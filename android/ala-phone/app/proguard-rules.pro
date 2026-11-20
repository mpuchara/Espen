# Android 8.1 compatibility: shrink unused library code but retain activity entrypoint.
-keep class pl.espen.garett.WatchActivity { *; }
# OkHttp probes optional TLS providers only when available.
-dontwarn org.conscrypt.**
-dontwarn org.bouncycastle.**
-dontwarn org.openjsse.**
