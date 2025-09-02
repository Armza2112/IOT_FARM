idf_component_register(
    SRCS "i2c_manage.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos log  i2cdev
)
