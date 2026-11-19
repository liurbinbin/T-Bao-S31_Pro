idf_component_register(SRCS "PixelRenderer.cpp" "PetModel.cpp" "PetLocale.cpp"
                       INCLUDE_DIRS ".")
# 复用指环的纯渲染代码；没有 Arduino、NVS 或显示驱动依赖。
