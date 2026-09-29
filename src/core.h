#ifndef BYEBYE_CORE_H
#define BYEBYE_CORE_H
#include <stdbool.h>
#include <stddef.h>
typedef enum {
  SUSPEND,
  LOCK,
  LOGOUT,
  SLEEP,
  SHUTDOWN,
  RESTART,
  HIBERNATE,
  ACTION_COUNT
} Action;

typedef struct {
  Action id;
  const char *label;
  char key;
} ActionInfo;

typedef struct {
  bool dry_run, simulate_error, list, help;
} Options;

extern const ActionInfo actions[ACTION_COUNT];
bool parse_options(int argc, char **argv, Options *options);
int action_for_key(const Action *visible, int count, unsigned int key);
int move_selection(int selected, int count, int delta);
/* Returns true when a safe test mode handled the request. Empty error =
 * success. */
bool mock_action(Options options, Action action, char *error, size_t size);
void print_help(void);
#endif
