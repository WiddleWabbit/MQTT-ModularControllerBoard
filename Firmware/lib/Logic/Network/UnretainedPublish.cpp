#include "UnretainedPublish.h"

/**
 * Deletes any retained broker copy of a topic once. A later call
 * with the same flag does nothing.
 *
 * An empty retained payload is the MQTT delete for a stored message.
 * A normal publish with retain off leaves that stored message in place.
 *
 * @param mqtt Connected-only publication path.
 * @param topic Topic name.
 * @param cleared Set true after the empty publish is accepted.
 * @return True when the topic is already clear, or the empty publish
 *         was accepted.
 */
bool clearRetainedOnce(MqttService& mqtt, const char* topic, bool& cleared)
{
  if (cleared)
  {
    return true;
  }
  if (!mqtt.publish(topic, "", true))
  {
    return false;
  }
  cleared = true;
  return true;
}

/**
 * Deletes any retained broker copy once, then publishes the payload
 * with retain off. A failed delete does not send the payload.
 *
 * @param mqtt Connected-only publication path.
 * @param topic Topic name.
 * @param payload Text payload.
 * @param cleared Set true after the broker copy has been deleted.
 * @return True when the payload was accepted.
 */
bool publishUnretained(MqttService& mqtt, const char* topic,
                       const char* payload, bool& cleared)
{
  if (!clearRetainedOnce(mqtt, topic, cleared))
  {
    return false;
  }
  return mqtt.publish(topic, payload, false);
}
