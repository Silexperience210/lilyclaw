plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.github.jvsena42.mandacaru"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.github.jvsena42.mandacaru"
        minSdk = 29
        targetSdk = 36
        versionCode = 8
        versionName = "0.5.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    kotlinOptions {
        jvmTarget = "11"
        // Required for ldk-node UniFFI coroutines interop
        freeCompilerArgs += listOf("-opt-in=kotlin.ExperimentalUnsignedTypes")
    }

    buildFeatures {
        compose = true
    }

    // ldk-node-android ships arm64-v8a + x86_64; align with Floresta's arm64-v8a only filter.
    // Remove x86_64 filter here to support emulators during development if desired.
    ndk {
        abiFilters += listOf("arm64-v8a")
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
            // Avoid duplicate .so conflicts between ldk-node-android and Floresta JNI libs
            pickFirsts += setOf("lib/arm64-v8a/libuniffi_ldk_node.so")
        }
    }
}

dependencies {
    // ── Core Android ──────────────────────────────────────────────────────────
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.activity.compose)

    // ── Compose ───────────────────────────────────────────────────────────────
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.material3)
    implementation(libs.androidx.compose.foundation)
    implementation(libs.androidx.compose.material.icons.extended)

    // ── Navigation ────────────────────────────────────────────────────────────
    implementation(libs.compose.navigation)

    // ── Storage & Preferences ────────────────────────────────────────────────
    implementation(libs.androidx.datastore.preferences)

    // ── Security: encrypted mnemonic storage ─────────────────────────────────
    // Uses Android Keystore; requires minSdk 23+. Our minSdk is 29.
    implementation("androidx.security:security-crypto:1.1.0-alpha06")

    // ── Networking (Floresta RPC) ─────────────────────────────────────────────
    implementation(libs.okhttp)
    implementation(libs.gson)

    // ── Dependency Injection ──────────────────────────────────────────────────
    implementation(platform(libs.koin.bom))
    implementation(libs.koin.core)
    implementation(libs.koin.viewmodel)
    implementation(libs.koin.compose)
    implementation(libs.koin.android)

    // ── Floresta FFI (existing) ───────────────────────────────────────────────
    implementation(libs.jna) { artifact { type = "aar" } }

    // ── ldk-node: Lightning node with UniFFI Kotlin bindings ─────────────────
    //
    // ldk-node uses Floresta's Electrum server (localhost) as its chain source.
    // The library bundles its own native .so (arm64-v8a + x86_64); we strip
    // x86_64 above via ndk.abiFilters to match Floresta's ARM64-only constraint.
    //
    // What it provides:
    //   • Full ldk-based Lightning node (channels, HTLCs, routing)
    //   • BDK-based on-chain wallet
    //   • LSPS2 JIT channel protocol (inbound liquidity via LSP)
    //   • BOLT-11 + BOLT-12 payment support
    //   • SQLite-backed persistence
    //   • Kotlin coroutine-friendly async events
    implementation("org.lightningdevkit:ldk-node-android:0.7.0")

    // ── QR code generation (for ReceivePaymentScreen) ─────────────────────────
    // Pure-Java ZXing core — no Android Activity dependency, works in Compose.
    implementation("com.google.zxing:core:3.5.3")

    // ── Testing ───────────────────────────────────────────────────────────────
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.ui.test.junit4)
}
