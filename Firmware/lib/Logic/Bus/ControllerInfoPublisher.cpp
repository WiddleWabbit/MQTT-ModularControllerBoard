#include "ControllerInfoPublisher.h"

#include <cstdio>

// ========== Construction ==========

ControllerInfoPublisher::ControllerInfoPublisher(MqttService& mqttService,
                                                 MqttTopicLayout& topics,
                                                 ControllerIdentity identity)
  : _mqttService(mqttService),
    _topics(topics),
    _identity(identity),
    _seenGeneration(topics.generation()),
    _publishedOk(false)
{
}


// ========== Public API ==========

/**
 * Publishes the identity until the broker accepts it.
 *
 * @return Nothing.
 */
void ControllerInfoPublisher::update()
{
  _syncTopicGeneration();
  if (_publishedOk)
  {
    return;
  }

  char payload[32];
  writeControllerIdentity(_identity, payload, sizeof(payload));
  if (!_mqttService.publish(_topics.controller(), payload, true))
  {
    return;
  }
  _publishedOk = true;
}


/**
 * Forgets the accepted publish when the device id has changed.
 *
 * @return Nothing.
 */
void ControllerInfoPublisher::_syncTopicGeneration()
{
  const uint32_t generation = _topics.generation();
  if (generation == _seenGeneration)
  {
    return;
  }
  _publishedOk = false;
  _seenGeneration = generation;
}
