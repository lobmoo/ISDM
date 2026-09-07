if(TARGET gtest)
  # 核心模块测试
  add_executable(primitive_type_test
    core/primitive_type_test.cc
  )

  target_link_libraries(primitive_type_test
    PRIVATE
      isdm_core
      gtest
      gtest_main
  )

  add_test(NAME PrimitiveTypeTest COMMAND primitive_type_test)

  # TODO: 添加更多测试
  # add_executable(type_descriptor_test core/type_descriptor_test.cc)
  # add_executable(schema_registry_test core/schema_registry_test.cc)
  # add_executable(object_store_test core/object_store_test.cc)

endif()
