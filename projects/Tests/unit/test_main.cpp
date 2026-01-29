#include <gtest/gtest.h>

/**
 * @brief Точка входа для запуска набора Google Test.
 * @param argc Количество аргументов командной строки.
 * @param argv Массив аргументов командной строки.
 * @return Код завершения, возвращаемый RUN_ALL_TESTS().
 * @note Собственный main используется, так как пакет gtest в vcpkg не устанавливает gtest_main.lib.
 * @warning Файл предназначен только для тестового исполняемого файла; не подключайте его в боевые библиотеки.
 */
int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
