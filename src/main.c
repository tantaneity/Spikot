#include "app/diagnostics.h"
#include "app/game.h"
#include <string.h>

#define FLAG_SNN_TEST "--snn-test"
#define FLAG_AGENT_TEST "--agent-test"
#define FLAG_LEARN_TEST "--learn-test"
#define FLAG_SPATIAL_TEST "--spatial-test"
#define FLAG_EXPORT "--export"
#define FLAG_SHOT "--shot"

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "";

    if (strcmp(mode, FLAG_SNN_TEST) == 0) return RunSnnTest();
    if (strcmp(mode, FLAG_AGENT_TEST) == 0) return RunAgentTest();
    if (strcmp(mode, FLAG_LEARN_TEST) == 0) return RunLearnTest();
    if (strcmp(mode, FLAG_SPATIAL_TEST) == 0) return RunSpatialTest();
    if (strcmp(mode, FLAG_EXPORT) == 0) return RunExport();
    if (strcmp(mode, FLAG_SHOT) == 0) return RunShot();

    return RunGame();
}
