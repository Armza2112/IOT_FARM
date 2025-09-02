idf_component_register(
    SRCS "pca9557_manage.c"
    INCLUDE_DIRS "."
    REQUIRES pca9557 i2cdev spiffs
)
