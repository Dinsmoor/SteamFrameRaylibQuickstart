// tests/toolbox - scenario files (*.sfxt in this folder) run against the real
// toolbox: its own setup, frame logic and drawing, compiled in unchanged (the
// toolbox_*.c files here just include them). docs/TESTING.md, "The test
// language".
//
//   make test T=workbench                     one file
//   make test T=workbench/push-sky-lever      one case

#include "sfxt.h"

void toolbox_setup(void);
void toolbox_logic(void);
void toolbox_draw(void);

int main(int argc, char **argv)
{
    return sfxt_scenario_main(argc, argv, "tests/toolbox", toolbox_logic, toolbox_setup, toolbox_draw);
}
