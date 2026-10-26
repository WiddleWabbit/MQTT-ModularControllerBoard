#pragma once

#include "MqttService.h"

/**
 * Deletes any retained broker copy of a topic once, then publishes
 * the payload with retain off.
 *
 * A later publish with retain off does not replace a message the
 * broker already stored. Result topics use this so a module that has
 * been unplugged cannot leave a reading for the next subscriber.
 * The first accepted call sends a retained empty payload, which MQTT
 * treats as a delete. If that publish fails, the payload is not sent
 * and cleared stays false.
 *
 * @param mqtt Connected-only publication path.
 * @param topic Topic name.
 * @param payload Text payload.
 * @param cleared Set true after the broker copy has been deleted.
 * @return True when the payload was accepted.
 */
bool publishUnretained(MqttService& mqtt, const char* topic,
                       const char* payload, bool& cleared);

/**
 * Deletes any retained broker copy of a topic once. A later call
 * with the same flag does nothing. Used for result topics this boot
 * never publishes a value on.
 *
 * @param mqtt Connected-only publication path.
 * @param topic Topic name.
 * @param cleared Set true after the empty publish is accepted.
 * @return True when the topic is already clear, or the empty publish
 *         was accepted.
 */
bool clearRetainedOnce(MqttService& mqtt, const char* topic, bool& cleared);
