#include "sierra/acsil/startup.hpp"

namespace sierra::acsil {

/**
 * @brief Выводит информацию о запуске DLL в Message Log Sierra Chart.
 * @param sc Контекст исследования, с помощью которого отправляем сообщение в лог.
 * @return void.
 * @note Сообщение выводится только один раз за загрузку DLL.
 * @warning Функция должна вызываться в момент начальной инициализации исследования.
 */
void LogDllStartup(SCStudyInterfaceRef sc) {
  static bool logged = false;
  if (logged) {
    return;
  }

  sc.AddMessageToLog("", 0);
  sc.AddMessageToLog("-------------", 0);
  sc.AddMessageToLog("SierraStudy DLL loaded", 0);
  logged = true;
}

}  // namespace sierra::acsil
