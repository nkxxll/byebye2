#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void) {
    Action visible[] = {LOCK, LOGOUT, SLEEP, SHUTDOWN, RESTART};
    CHECK(action_for_key(visible, 5, '1') == 0);
    CHECK(action_for_key(visible, 5, '5') == 4);
    CHECK(action_for_key(visible, 5, '6') == -1);
    CHECK(action_for_key(visible, 5, 'S') == 2);
    CHECK(action_for_key(visible, 5, 'p') == 3);
    CHECK(action_for_key(visible, 5, 'h') == -1);
    CHECK(action_for_key(visible, 0, '1') == -1);
    CHECK(move_selection(0, 5, -1) == 4);
    CHECK(move_selection(4, 5, 1) == 0);
    CHECK(move_selection(0, 0, 1) == -1);
    Options options;
    char *args[] = {"byebye", "--dry-run", "--list"};
    CHECK(parse_options(3, args, &options) && options.dry_run && options.list);
    char error[256];
    CHECK(mock_action(options, SHUTDOWN, error, sizeof(error)) && !error[0]);
    options.simulate_error = true;
    CHECK(mock_action(options, SHUTDOWN, error, sizeof(error)) && strstr(error, "Simulated"));
    char *bad[] = {"byebye", "--shutdown"};
    CHECK(!parse_options(2, bad, &options));
    CHECK(!mock_action(options, SHUTDOWN, error, sizeof(error)));
    puts("Core tests passed.");
}
