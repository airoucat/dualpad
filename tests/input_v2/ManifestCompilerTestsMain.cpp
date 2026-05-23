#include "pch.h"

#include <iostream>
#include <stdexcept>

void RunContextCatalogTests();
void RunLegacyIniImporterTests();
void RunActionManifestTests();
void RunManifestValidatorTests();
void RunAtomicConfigReloaderTests();

int main()
{
    try {
        RunLegacyIniImporterTests();
        RunContextCatalogTests();
        RunActionManifestTests();
        RunManifestValidatorTests();
        RunAtomicConfigReloaderTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

