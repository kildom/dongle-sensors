

TODO: protocol:
 * output packet (SENSOR -> HUB)
   * retry counter and power level (for connection quality statistics)
   * current real time (unix time stamp) if known (uint32)
   ```c
    typedef struct {
    	uint32_t address_low;
    	uint16_t address_high;
    	int16_t temp;
      uint32_t timestamp; // 0 if unknown
    	int16_t voltage;
      int8_t power;
      uint8_t retry_counter;
    } OutputPacket; // 16 bytes
   ```
 * input packet (HUB -> SENSOR)
   * request faster updates (5sec.) and full TX power for 1 minute
   * set current real time (unix time stamp)
   ```c
    typedef struct {
    	uint32_t address_low;
    	uint16_t address_high;
    	uint16_t flags; // bit: 0 - ACK, 1 - force fast updates
      uint32_t timestamp; // 0 if unknown
      uint32_t _reserved;
    } InputPacket; // 16 bytes
   ```
