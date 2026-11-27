cmake_minimum_required(VERSION 3.16)
project(BasecampInstallerCommon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Where logos-package's headers are (<logos/semver.hpp>).
set(LOGOS_PACKAGE_INCLUDE_DIR "" CACHE PATH "logos-package headers")

find_package(Qt6 REQUIRED COMPONENTS Core Network Qml)
# The trust sources' lookalike checks: ICU's spoof checker.
find_package(ICU REQUIRED COMPONENTS uc i18n)

include(cmake/BasecampInstallerCommon.cmake)

add_library(basecamp_installer_common STATIC
    ${BASECAMP_INSTALLER_COMMON_SOURCES} ${BASECAMP_INSTALLER_COMMON_TRUST_SOURCES}
    ${BASECAMP_INSTALLER_COMMON_PACKAGE_TRUST_SOURCES})
target_include_directories(basecamp_installer_common
    PUBLIC ${BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS}
    PRIVATE ${LOGOS_PACKAGE_INCLUDE_DIR})
target_link_libraries(basecamp_installer_common PUBLIC Qt6::Core Qt6::Network ICU::i18n ICU::uc)
target_compile_options(basecamp_installer_common PRIVATE -Wall -Wextra -Werror)

enable_testing()
foreach(t availability row_state package_rows repository_labels types
        install_progress package_model package_filter paging_proxy
        catalog_client icon_fetcher online_watch
        approval installer intent_consent package_trust)
    add_executable(tst_${t} tests/cpp/tst_${t}.cpp)
    target_link_libraries(tst_${t} PRIVATE basecamp_installer_common Qt6::Qml)
    target_compile_definitions(tst_${t} PRIVATE
        INSTALLER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml/Installer")
    add_test(NAME ${t} COMMAND tst_${t})
endforeach()
# The trust sources on their own, as Basecamp builds them: nothing else of the
# library, only Qt Core and ICU.
add_executable(tst_trust tests/cpp/tst_trust.cpp ${BASECAMP_INSTALLER_COMMON_TRUST_SOURCES})
target_include_directories(tst_trust PRIVATE ${BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS})
target_link_libraries(tst_trust PRIVATE Qt6::Core ICU::i18n ICU::uc)
target_compile_options(tst_trust PRIVATE -Wall -Wextra -Werror)
add_test(NAME trust COMMAND tst_trust)
