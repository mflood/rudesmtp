add_executable(sendmail sendmail.cpp)
target_link_libraries(sendmail PRIVATE rudesmtp::rudesmtp)
