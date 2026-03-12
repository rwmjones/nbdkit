/* go-nbd interoperability test
 * Copyright Red Hat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

package go_nbd_test

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"

	"github.com/digitalocean/go-nbd"
)

const sizeMB int = 10

func TestGoNBDUnix(t *testing.T) {
	d := t.TempDir()
	socket := filepath.Join(d, "nbd-unix.sock")
	pidfile := filepath.Join(d, "nbd-unix.pid")
	uriS := fmt.Sprintf("nbd+unix:///?socket=%s", socket)

	ctx, cancel := context.WithCancel(context.Background())

	args := []string{
		"-f", "--exit-with-parent",
		"-U", socket,
		"-P", pidfile,
		"memory",
		fmt.Sprintf("%dM", sizeMB),
	}
	t.Logf("running: nbdkit %v", args)
	cmd := exec.CommandContext(ctx, "nbdkit", args...)
	err := cmd.Start()
	if err != nil {
		t.Fatalf("error starting nbdkit %v: %v", args, err)
	}
	defer func() {
		cancel()
		err = cmd.Wait()
		t.Log("stdout", cmd.Stdout)
		t.Log("stderr", cmd.Stderr)
	}()

	// Wait for nbdkit to start.
	for i := 0; i < 60; i++ {
		_, err := os.Stat(pidfile)
		if err == nil {
			break
		}
		if os.IsNotExist(err) {
			time.Sleep(1000 * time.Millisecond)
			continue
		}
		t.Fatalf("failed waiting for pid file: %v", err)
	}

	// Connect to nbdkit.
	uri, err := nbd.ParseURI(uriS)
	if err != nil {
		t.Fatalf("parse NBD URI %s: %v", uriS, err)
	}

	dialer := new(nbd.Dialer)
	conn, err := dialer.Dial(ctx, uri)
	if err != nil {
		t.Fatalf("dial: %s: %v", uriS, err)
	}

	defer func() {
		t.Helper()
		if err := conn.Close(); err != nil {
			t.Errorf("close: %v", err)
		}
	}()

	err = conn.Connect()
	if err != nil {
		t.Fatalf("connect: %s: %v", uriS, err)
	}

	defer func() {
		t.Helper()
		if err := conn.Abort(); err != nil {
			t.Errorf("abort: %v", err)
		}
	}()

	//err = conn.StructuredReplies()
	//if err != nil {
	//        t.Errorf("set structured replies: %s: %v", uriS, err)
	//}

	// Finish option negotiation.
	var info nbd.ExportInfo
	err = conn.Go("", nbd.InfoRequestAll(), func(i nbd.ExportInfo) error {
		info = i
		return nil
	})
	if err != nil {
		t.Fatalf("go: %s: %v", uriS, err)
	}

	if info.ValidName {
		t.Logf("Name: %s", info.Name)
	}
	if info.ValidDescription {
		t.Logf("Description: %s", info.Description)
	}
	if info.ValidExport {
		t.Logf("Size: %d", info.Size)
		t.Logf("TransmissionFlags: %d", info.TransmissionFlags)
	}
	if info.ValidBlockSize {
		t.Logf("MinBlockSize: %d", info.MinBlockSize)
		t.Logf("PreferredBlockSize: %d", info.PreferredBlockSize)
		t.Logf("MaxBlockSize: %d", info.MaxBlockSize)
	}

	// Get the size.
	if info.Size != uint64(sizeMB*1024*1024) {
		t.Errorf("unexpected size (%d != %dM)",
			info.Size, sizeMB)
	}

	// Write something and read it back.
	data := make([]byte, 512)
	for i := 0; i < len(data); i++ {
		data[i] = byte(i ^ 0x55)
	}
	err = conn.Write(data, 0, nbd.CommandFlags(0))
	if err != nil {
		t.Errorf("write: %s: %v", uriS, err)
	}
	data2 := make([]byte, len(data))
	n, err := conn.Read(data2, 0, nbd.CommandFlags(0))
	if err != nil {
		t.Errorf("read: %s: %v", uriS, err)
	}
	// The return value from conn.Read is broken:
	// https://github.com/digitalocean/go-nbd/issues/52
	//if n != len(data2) {
	//	t.Errorf("short read: %d != %d", n, len(data2))
	//}
	_ = n
	if !bytes.Equal(data, data2) {
		t.Errorf("data written and read do not match")
	}
}
