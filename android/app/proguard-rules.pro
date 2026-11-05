# kotlinx.serialization
-keepattributes *Annotation*, InnerClasses
-dontnote kotlinx.serialization.AnnotationsKt
-keepclassmembers class kotlinx.serialization.json.** {
    *** Companion;
}
-keepclasseswithmembers class kotlinx.serialization.json.** {
    kotlinx.serialization.KSerializer serializer(...);
}
-keep,includedescriptorclasses class com.crid.app.**$$serializer { *; }
-keepclassmembers class com.crid.app.** {
    *** Companion;
}
-keepclasseswithmembers class com.crid.app.** {
    kotlinx.serialization.KSerializer serializer(...);
}
