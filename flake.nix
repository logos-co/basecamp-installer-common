{
  description = "What Logos' two installer apps share: package rows and their rules, and the words for them";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
    # Headers only: <logos/semver.hpp>, the version order lgx, lgpm and lgpd use.
    logos-package = {
      url = "github:logos-co/logos-package";
      inputs.logos-nix.follows = "logos-nix";
    };
  };

  outputs = { self, nixpkgs, logos-nix, logos-package }:
    let
      forAll = logos-nix.lib.forAllSystems;
      semverHeaders = system: logos-package.packages.${system}.headers;

      # What a consumer stages and compiles in: include/, src/, cmake/, the
      # JavaScript under qml/Installer, and tools/shared-words-check.
      source = pkgs: pkgs.runCommand "basecamp-installer-common-src" { } ''
        mkdir -p $out
        cp -r ${./include} $out/include
        cp -r ${./src} $out/src
        cp -r ${./cmake} $out/cmake
        cp -r ${./qml} $out/qml
        cp -r ${./tools} $out/tools
      '';
    in {
      packages = forAll ({ pkgs, system }: {
        default = source pkgs;
      });

      checks = forAll ({ pkgs, system }: {
        unit = pkgs.stdenv.mkDerivation {
          name = "basecamp-installer-common-unit-tests";
          src = ./.;
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja ];
          buildInputs = [ pkgs.qt6.qtbase pkgs.qt6.qtdeclarative ];
          dontWrapQtApps = true;
          cmakeFlags = [ "-DLOGOS_PACKAGE_INCLUDE_DIR=${semverHeaders system}/include" ];
          doCheck = true;
          QT_QPA_PLATFORM = "offscreen";
          checkPhase = "ctest --output-on-failure --verbose";
          installPhase = "touch $out";
        };

        shared-words = pkgs.runCommand "basecamp-installer-common-shared-words-check" {
          nativeBuildInputs = [ pkgs.python3 ];
        } ''
          cp -r ${./.} src && chmod -R u+w src
          bash src/tests/shared-words/run.sh
          touch $out
        '';

        qml = pkgs.runCommand "basecamp-installer-common-qml-tests" {
          nativeBuildInputs = [ pkgs.qt6.qtdeclarative ];
          QT_QPA_PLATFORM = "offscreen";
          QT_PLUGIN_PATH = "${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}";
          QML_IMPORT_PATH = "${pkgs.qt6.qtdeclarative}/${pkgs.qt6.qtbase.qtQmlPrefix}";
        } ''
          cp -r ${./.} src && chmod -R u+w src
          ${pkgs.qt6.qtdeclarative}/bin/qmltestrunner -input src/tests/qml
          touch $out
        '';
      });
    };
}
