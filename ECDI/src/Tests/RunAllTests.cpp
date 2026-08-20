#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"

using namespace ECDI::Test;

void ECDI::Test::RunAllTests()
{
    RunRendererTests();
    RunWidgetTests();
    RunLayoutTests();
    RunTextBoxTests();
    // RunEventTests();  // P2
}