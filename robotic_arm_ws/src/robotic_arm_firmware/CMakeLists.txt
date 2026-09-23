cmake_minimum_required(VERSION 3.8)
project(robotic_arm_firmware)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rclpy REQUIRED)

# Sirve para que Python la encuentre con import
ament_python_install_package(${PROJECT_NAME}) 

# Copia el .py a install/<paquete>/lib/<paquete>/ con permiso de ejecución.
install(PROGRAMS
  ${PROJECT_NAME}/simple_serial_transmitter.py
  ${PROJECT_NAME}/simple_serial_receiver.py
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
