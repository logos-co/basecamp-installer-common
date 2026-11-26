// Whether this host is online, for the download notice.

#include "check.h"

#include <logos/installer/OnlineWatch.h>

using namespace logos::installer;

TEST(the_test_switch_decides) {
    qputenv("BASECAMP_FORCE_OFFLINE", "1");
    CHECK(!OnlineWatch().isOnline());
    qputenv("BASECAMP_FORCE_OFFLINE", "0");
    CHECK(OnlineWatch().isOnline());
    qunsetenv("BASECAMP_FORCE_OFFLINE");
}

CHECK_MAIN()
