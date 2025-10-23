#include "sierra/acsil/supportFunction.hpp"

namespace sierra::acsil {

/**
 * @brief Выводит в Message Log отметку о загрузке DLL SierraStudy.
 * @param sc Интерфейс исследования, предоставляемый Sierra Chart.
 * @return void Функция не возвращает значение.
 * @note Локальный статический флаг защищает от повторного добавления сообщений.
 * @warning Вызывайте функцию пока DLL инициализируется, чтобы иметь маркер в логе.
 */
void LogDllStartup(SCStudyInterfaceRef sc) {
  static bool logged = false;
  if (logged) {
    return;
  }

  sc.AddMessageToLog("                                      ", 0);
  sc.AddMessageToLog("--------------------------------------", 0);
  sc.AddMessageToLog("          SierraStudy DLL loaded      ", 0);
  logged = true;
}

}  // namespace sierra::acsil
