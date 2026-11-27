# Compile the library into a backend:
#   include(<staged>/cmake/BasecampInstallerCommon.cmake)
#   SOURCES ... ${BASECAMP_INSTALLER_COMMON_SOURCES}
#   INCLUDE_DIRS ... ${BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS}
# The consumer also puts logos-package's headers (<logos/semver.hpp>) on its
# include path, links Qt6::Core and Qt6::Network, and builds with AUTOMOC: the
# headers with Q_OBJECT are listed so it finds them.
#
# Who signed a package (Trust.h), for a consumer that shows it: add
# ${BASECAMP_INSTALLER_COMMON_TRUST_SOURCES} and link ICU, as Qt does:
#   find_package(ICU REQUIRED COMPONENTS uc i18n) ... ICU::i18n ICU::uc
# They need nothing else of the library, so Basecamp builds them alone. An app
# that compiles both judges what an app brings (PackageTrust.h) with
# ${BASECAMP_INSTALLER_COMMON_PACKAGE_TRUST_SOURCES} too.
get_filename_component(_bic_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(BASECAMP_INSTALLER_COMMON_INCLUDE_DIRS "${_bic_root}/include")
set(BASECAMP_INSTALLER_COMMON_SOURCES
    "${_bic_root}/include/logos/installer/CatalogClient.h"
    "${_bic_root}/include/logos/installer/IconFetcher.h"
    "${_bic_root}/include/logos/installer/InstallProgress.h"
    "${_bic_root}/include/logos/installer/Installer.h"
    "${_bic_root}/include/logos/installer/IntentConsent.h"
    "${_bic_root}/include/logos/installer/OnlineWatch.h"
    "${_bic_root}/include/logos/installer/PackageFilter.h"
    "${_bic_root}/include/logos/installer/PackageModel.h"
    "${_bic_root}/include/logos/installer/PagingProxy.h"
    "${_bic_root}/src/Approval.cpp"
    "${_bic_root}/src/Availability.cpp"
    "${_bic_root}/src/CatalogClient.cpp"
    "${_bic_root}/src/IconFetcher.cpp"
    "${_bic_root}/src/InstallProgress.cpp"
    "${_bic_root}/src/Installer.cpp"
    "${_bic_root}/src/IntentConsent.cpp"
    "${_bic_root}/src/OnlineWatch.cpp"
    "${_bic_root}/src/PackageFilter.cpp"
    "${_bic_root}/src/PackageModel.cpp"
    "${_bic_root}/src/PackageRows.cpp"
    "${_bic_root}/src/PagingProxy.cpp"
    "${_bic_root}/src/RepositoryLabels.cpp"
    "${_bic_root}/src/RowState.cpp"
    "${_bic_root}/src/Types.cpp"
)
set(BASECAMP_INSTALLER_COMMON_TRUST_SOURCES
    "${_bic_root}/include/logos/installer/Trust.h"
    "${_bic_root}/src/Trust.cpp"
)
set(BASECAMP_INSTALLER_COMMON_PACKAGE_TRUST_SOURCES
    "${_bic_root}/include/logos/installer/PackageTrust.h"
    "${_bic_root}/src/PackageTrust.cpp"
)
set(BASECAMP_INSTALLER_COMMON_QML_DIR "${_bic_root}/qml")
unset(_bic_root)
