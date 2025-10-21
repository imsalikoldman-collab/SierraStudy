# Core Usage Example

```cpp
#include "sierra/core/moving_average.hpp"

#include <iostream>
#include <vector>

int main() {
  const std::vector<double> prices{1.0, 2.0, 3.0, 4.0, 5.0};
  const std::size_t period = 3;

  const auto ma = sierra::core::moving_average(prices, period);
  for (double value : ma) {
    std::cout << value << '\n';
  }
}
```
