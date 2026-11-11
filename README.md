# TRIDOS x JZ - StatTrak


```sh
# Release build
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.release" build

# Debug build
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug" build

# Build
idf.py build

# Flash
idf.py flash

# Monitor
idf.py monitor
```