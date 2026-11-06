plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
    id("org.jetbrains.kotlin.plugin.serialization")
}

// 版本号默认值，CI 可用 -PappVersion=2.0.2 -PappVersionCode=202 覆盖，
// 保证 tag（v2.0.2）与 APK 内部 versionName 一致——否则应用内更新会一直提示有新版。
// versionCode 必须与 CI 用同一个公式（major*10000 + minor*100 + patch），
// 否则本地编的包和 Release 里的包会互相「版本降级」，覆盖安装被系统拒绝。
val appVersion = (project.findProperty("appVersion") as String?) ?: "2.0.1"
// versionCode 必须是正整数：非 tag 的 main 分支构建 CI 会传 0.0.0 → code 0，
// gradle 直接拒绝配置（android.defaultConfig.versionCode ... positive integer），
// 这里兜底抬到 1，只为让普通提交的流水线能编出 APK 做快速验证。
val appVersionCode = (
    (project.findProperty("appVersionCode") as String?)?.toIntOrNull()
        ?: appVersion.split(".").let { p ->
            (p.getOrNull(0)?.toIntOrNull() ?: 0) * 10000 +
                (p.getOrNull(1)?.toIntOrNull() ?: 0) * 100 +
                (p.getOrNull(2)?.toIntOrNull() ?: 0)
        }
    ).coerceAtLeast(1)

android {
    namespace = "com.crid.app"
    compileSdk = 35

    signingConfigs {
        // 固定签名：本地与 CI 共用同一把 key。
        // 应用内更新要覆盖安装，签名一变系统就会拒绝（INSTALL_FAILED_UPDATE_INCOMPATIBLE）。
        create("crid") {
            storeFile = rootProject.file("keystore/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    defaultConfig {
        applicationId = "com.crid.app"
        minSdk = 26
        targetSdk = 35
        versionCode = appVersionCode
        versionName = appVersion
    }

    buildTypes {
        debug {
            signingConfig = signingConfigs.getByName("crid")
        }
        release {
            signingConfig = signingConfigs.getByName("crid")
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

dependencies {
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("org.osmdroid:osmdroid-android:6.1.20")
    debugImplementation("androidx.compose.ui:ui-tooling")
}
