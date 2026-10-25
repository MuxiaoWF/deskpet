// Top-level build file where you can add configuration options common to all sub-projects/modules.
plugins {
    alias(libs.plugins.android.application) apply false
    idea
    id("io.github.ben-manes.versions") version "0.66.0"
}

// Exclude the Cubism SDK submodule from IDE indexing/inspections.
// The framework is compiled by CMake (app/CMakeLists.txt add_subdirectory),
// so this only affects IntelliJ/Android Studio tooling, never the build.
// Declared here (instead of the IDE UI) so the exclusion survives Gradle
// sync and applies to every collaborator.
idea {
    module {
        excludeDirs = excludeDirs + file("Framework")
    }
}