/* nbdkit
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>

#include <nbdkit-filter.h>

#include "rules.h"

static struct rule *allow_rules, *allow_rules_last;
static struct rule *deny_rules, *deny_rules_last;

/* If there are any dn:/issuer-dn: rules then we must do more
 * expensive "late filtering" in the .open method.
 */
bool late_filtering = false;

static void
ip_unload (void)
{
  free_rules (allow_rules);
  free_rules (deny_rules);
}

static int
ip_config (nbdkit_next_config *next, nbdkit_backend *nxdata,
           const char *key, const char *value)
{
  /* For convenience we permit multiple allow and deny parameters,
   * which append rules to the end of the respective list.
   */
  if (strcmp (key, "allow") == 0) {
    if (parse_rules (key, &allow_rules, &allow_rules_last, value) == -1)
      return -1;
    return 0;
  }
  else if (strcmp (key, "deny") == 0) {
    if (parse_rules (key, &deny_rules, &deny_rules_last, value) == -1)
      return -1;
    return 0;
  }

  return next (nxdata, key, value);
}

static int
ip_config_complete (nbdkit_next_config_complete *next, nbdkit_backend *nxdata)
{
  /* These are only printed if -D ip.rules=1 */
  print_rules ("ip: parsed allow", allow_rules);
  print_rules ("ip: parsed deny", deny_rules);

  return next (nxdata);
}

#define ip_config_help \
  "allow=addr[,addr...]     Set allow list.\n" \
  "deny=addr[,addr...]      Set deny list."

/* Normal/early filtering, if there are no dn:/issuer-dn: rules. */
static int
ip_preconnect (nbdkit_next_preconnect *next, nbdkit_backend *nxdata,
               int readonly)
{
  if (!late_filtering) {
    /* Follow the rules. */
    if (check_if_allowed (allow_rules, deny_rules) == false) {
      nbdkit_error ("client not permitted to connect "
                    "because of source address restriction");
      return -1;
    }
  }

  if (next (nxdata, readonly) == -1)
    return -1;

  return 0;
}

/* Late filtering. */
static int
ip_list_exports (nbdkit_next_list_exports *next, nbdkit_backend *nxdata,
                 int readonly, int is_tls,
                 struct nbdkit_exports *exports)
{
  if (late_filtering) {
    /* Follow the rules. */
    if (check_if_allowed (allow_rules, deny_rules) == false) {
      nbdkit_error ("client not permitted to connect "
                    "because of source address restriction");
      return -1;
    }
  }

  return next (nxdata, readonly, exports);
}

/* Late filtering. */
static void *
ip_open (nbdkit_next_open *next, nbdkit_context *nxdata,
         int readonly, const char *exportname, int is_tls)
{
  if (late_filtering) {
    /* Follow the rules. */
    if (check_if_allowed (allow_rules, deny_rules) == false) {
      nbdkit_error ("client not permitted to connect "
                    "because of source address restriction");
      return NULL;
    }
  }

  if (next (nxdata, readonly, exportname) == -1)
    return NULL;

  return NBDKIT_HANDLE_NOT_NEEDED;
}

static struct nbdkit_filter filter = {
  .name              = "ip",
  .longname          = "nbdkit ip filter",
  .unload            = ip_unload,
  .config            = ip_config,
  .config_complete   = ip_config_complete,
  .config_help       = ip_config_help,
  .preconnect        = ip_preconnect,
  .list_exports      = ip_list_exports,
  .open              = ip_open,
};

NBDKIT_REGISTER_FILTER (filter)
