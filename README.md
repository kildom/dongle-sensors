

TODO: protocol:
 * output packet (SENSOR -> HUB)
   * retry counter (for connection quality statistics)
   * current real time (unix time stamp) if known
 * input packet (HUB -> SENSOR)
   * request faster updates (5sec.) for 1 minute
   * set current real time (unix time stamp)
