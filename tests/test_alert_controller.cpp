#include <cassert>
#include <cstdint>
#include <iostream>

#include "alert_controller.h"

int main() {
  // Trigger threshold is inclusive, and the hold timer must reach its exact
  // configured boundary before an alert is emitted.
  AlertController boundaries(70.0F, 65.0F, 1000U, 2000U, 500U);
  assert(!boundaries.update(69.9F, 0U));
  assert(boundaries.state() == AlertState::MONITORING);
  assert(!boundaries.update(70.0F, 10U));
  assert(boundaries.state() == AlertState::THRESHOLD_HOLD);
  assert(!boundaries.update(70.0F, 1009U));
  assert(boundaries.update(70.0F, 1010U));
  assert(boundaries.state() == AlertState::PLAYING_ALERT);
  assert(boundaries.alertCount() == 1U);

  // Cooldown is inclusive at its exact boundary. High noise moves the
  // controller to WAITING_FOR_QUIET, not back to MONITORING.
  boundaries.onPlaybackCompleted(2000U);
  assert(!boundaries.update(80.0F, 3999U));
  assert(boundaries.state() == AlertState::COOLDOWN);
  assert(!boundaries.update(80.0F, 4000U));
  assert(boundaries.state() == AlertState::WAITING_FOR_QUIET);
  assert(!boundaries.update(80.0F, 5000U));
  assert(boundaries.state() == AlertState::WAITING_FOR_QUIET);
  assert(boundaries.alertCount() == 1U);

  // Equality with the reset threshold qualifies as quiet (<= reset threshold).
  assert(!boundaries.update(65.0F, 5001U));
  assert(!boundaries.update(65.0F, 5500U));
  assert(boundaries.state() == AlertState::WAITING_FOR_QUIET);
  assert(!boundaries.update(65.0F, 5501U));
  assert(boundaries.state() == AlertState::MONITORING);

  // A new sustained high-noise event can trigger after re-arming.
  assert(!boundaries.update(72.0F, 5600U));
  assert(!boundaries.update(72.0F, 6599U));
  assert(boundaries.update(72.0F, 6600U));
  assert(boundaries.alertCount() == 2U);

  // A brief fall below the reset threshold cancels the pending hold and a
  // subsequent high event starts a fresh hold interval.
  AlertController cancellation(70.0F, 65.0F, 3000U, 10000U, 2000U);
  assert(!cancellation.update(72.0F, 1000U));
  assert(!cancellation.update(64.9F, 2000U));
  assert(cancellation.state() == AlertState::MONITORING);
  assert(!cancellation.update(72.0F, 2500U));
  assert(!cancellation.update(72.0F, 5499U));
  assert(cancellation.update(72.0F, 5500U));

  // Unsigned elapsed-time arithmetic remains valid across millis() rollover.
  AlertController rollover(70.0F, 65.0F, 3000U, 10000U, 2000U);
  const uint32_t nearWrap = UINT32_MAX - 1000U;
  assert(!rollover.update(72.0F, nearWrap));
  assert(rollover.update(72.0F, 1999U));

  std::cout << "All AlertController tests passed.\n";
  return 0;
}
