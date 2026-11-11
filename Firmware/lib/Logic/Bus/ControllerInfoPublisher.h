#pragma once

#include <cstdint>

#include "ControllerIdentity.h"
#include "MqttService.h"
#include "MqttTopicLayout.h"

/**
 * Publishes the controller board and firmware revisions on {id}/controller.
 * The payload is retained. A rejected publish stays pending. A new device
 * id sends the same text once on the new topic.
 */
class ControllerInfoPublisher
{
public:
  /**
   * Creates a publisher for one controller identity.
   *
   * @param mqttService Connected-only publication path.
   * @param topics Topic tree. The line is published at "{id}/controller".
   * @param identity Board and firmware revisions from the composition root.
   */
  ControllerInfoPublisher(MqttService& mqttService, MqttTopicLayout& topics,
                          ControllerIdentity identity);

  /**
   * Publishes the identity until the broker accepts it.
   *
   * Does nothing when the last publish was accepted and the device id
   * is unchanged.
   *
   * @return Nothing.
   */
  void update();

private:
  MqttService& _mqttService;
  MqttTopicLayout& _topics;
  ControllerIdentity _identity;
  uint32_t _seenGeneration;
  bool _publishedOk;

  /**
   * Forgets the accepted publish when the device id has changed.
   *
   * @return Nothing.
   */
  void _syncTopicGeneration();
};
