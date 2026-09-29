#include "core.h"
#include <stdio.h>
#include <string.h>
const ActionInfo actions[ACTION_COUNT] = {
    {SUSPEND, "Suspend", 's'},    {LOCK, "Lock", 'l'},
    {LOGOUT, "Logout", 'o'},      {SLEEP, "Sleep", 's'},
    {SHUTDOWN, "Shutdown", 'p'},  {RESTART, "Restart", 'r'},
    {HIBERNATE, "Hibernate", 'h'}};

bool parse_options(int argc, char **argv, Options *options) {
  memset(options, 0, sizeof(*options));
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--dry-run"))
      options->dry_run = true;
    else if (!strcmp(argv[i], "--simulate-error"))
      options->simulate_error = true;
    else if (!strcmp(argv[i], "--list"))
      options->list = true;
    else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
      options->help = true;
    else
      return false;
  }
  return true;
}
int action_for_key(const Action *visible, int count, unsigned int key) {
  if (key >= 'A' && key <= 'Z')
    key += 'a' - 'A';
  if (key >= '1' && key <= '9')
    return (int)(key - '1') < count ? (int)(key - '1') : -1;
  for (int i = 0; i < count; i++)
    if (key == (unsigned int)actions[visible[i]].key)
      return i;
  return -1;
}
int move_selection(int selected, int count, int delta) {
  if (count <= 0)
    return -1;
  int next = (selected + delta) % count;
  return next < 0 ? next + count : next;
}
bool mock_action(Options options, Action action, char *error, size_t size) {
  if (!options.dry_run && !options.simulate_error)
    return false;
  if (options.simulate_error)
    snprintf(error, size,
             "Simulated %s failure. No system action was performed.",
             actions[action].label);
  else {
    if (size)
      error[0] = '\0';
    printf("Dry run: %s (no system action performed)\n", actions[action].label);
  }
  return true;
}
void print_help(void) {
  puts("Byebye — native session and power menu\n"
       "Usage: byebye [--dry-run | --simulate-error] [--list] [--help]\n"
       "  --dry-run          Close on selection without performing the action\n"
       "  --simulate-error   Show a test error instead of performing the "
       "action\n"
       "  --list             List available actions without opening a window\n"
       "Keys: numbers or action letters; arrows/Tab to navigate; Enter to "
       "select; Esc/q to quit.");
}
