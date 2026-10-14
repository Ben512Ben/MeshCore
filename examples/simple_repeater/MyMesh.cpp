#include "MyMesh.h"
#include <algorithm>

/* ------------------- Sleep cycle / battery settings ------------------- */

#define BATT_CHECK_MS     (5UL * 60UL * 1000UL)   // battery check interval
#define BATT_LOW_MV       3500                    // start sleep cycle below this (3 checks in a row)
#define BATT_LOW_COUNT    3
#define BATT_CRIT_MV      3200                    // last resort: reboot below this (2 checks in a row)
#define BATT_CRIT_COUNT   2                       //   so the board's boot lock takes over
#define CYCLE_STOP_MV     4100                    // sleep cycle ends at/above this
#define CYCLE_SLEEP_MS    (57UL * 60UL * 1000UL)  // sleep cycle: radio off
#define CYCLE_AWAKE_MS    (3UL * 60UL * 1000UL)   // sleep cycle: radio on
#define BRISBANE_UTC_OFFSET_SECS  (10L * 3600L)   // 'sleep until' uses Brisbane time (UTC+10, no DST)
#define CLOCK_VALID_AFTER 1767225600UL            // 1 Jan 2026: any earlier clock is considered 'not set'
#define CLI_REPLY_MAX     150                     // keep replies well inside the 160 byte CLI buffer
#define HOT_START_C       60                      // chip temperature warning: 60C or more ...
#define HOT_COUNT         3                       //   ... for 3 checks in a row
#define HOT_RESUME_C      50                      //   re-armed once it falls below 50C
#define CRIT_WARN_MV      3300                    // 'battery critical' channel warning below this
#define REPORT_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)  // daily report: every 24h from boot
#define CHAN_GAP_MS       60000                   // gap between automatic channel messages
#define CHAN_PAIR_GAP_MS  10000                   // gap between the two daily report messages
#define BLOCK_NOTICE_GAP_MS (60UL * 60UL * 1000UL)  // at most one auto-block notice per hour
#define SLEEP_MSG_WAIT_MS 120000                  // sleep waits at most this long for queued channel messages
#define TX_QUIET_MS       6000                    // radio never goes to sleep until this long after the last queued send

/* ------------------------------ Config -------------------------------- */

#ifndef LORA_FREQ
  #define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
  #define LORA_BW 250
#endif
#ifndef LORA_SF
  #define LORA_SF 10
#endif
#ifndef LORA_CR
  #define LORA_CR 5
#endif
#ifndef LORA_TX_POWER
  #define LORA_TX_POWER 20
#endif

#ifndef ADVERT_NAME
  #define ADVERT_NAME "repeater"
#endif
#ifndef ADVERT_LAT
  #define ADVERT_LAT 0.0
#endif
#ifndef ADVERT_LON
  #define ADVERT_LON 0.0
#endif

#ifndef ADMIN_PASSWORD
  #define ADMIN_PASSWORD "password"
#endif

#ifndef SERVER_RESPONSE_DELAY
  #define SERVER_RESPONSE_DELAY 300
#endif

#ifndef TXT_ACK_DELAY
  #define TXT_ACK_DELAY 200
#endif

#define FIRMWARE_VER_LEVEL       2

#define REQ_TYPE_GET_STATUS         0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_NEIGHBOURS     0x06
#define REQ_TYPE_GET_OWNER_INFO     0x07     // FIRMWARE_VER_LEVEL >= 2

#define RESP_SERVER_LOGIN_OK        0 // response to ANON_REQ

#define ANON_REQ_TYPE_REGIONS      0x01
#define ANON_REQ_TYPE_OWNER        0x02
#define ANON_REQ_TYPE_BASIC        0x03   // just remote clock

#define CLI_REPLY_DELAY_MILLIS      600

#define LAZY_CONTACTS_WRITE_DELAY    5000

void MyMesh::putNeighbour(const mesh::Identity &id, uint32_t timestamp, float snr) {
#if MAX_NEIGHBOURS // check if neighbours enabled
  // find existing neighbour, else use least recently updated
  uint32_t oldest_timestamp = 0xFFFFFFFF;
  NeighbourInfo *neighbour = &neighbours[0];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    // if neighbour already known, we should update it
    if (id.matches(neighbours[i].id)) {
      neighbour = &neighbours[i];
      break;
    }

    // otherwise we should update the least recently updated neighbour
    if (neighbours[i].heard_timestamp < oldest_timestamp) {
      neighbour = &neighbours[i];
      oldest_timestamp = neighbour->heard_timestamp;
    }
  }

  // update neighbour info
  neighbour->id = id;
  neighbour->advert_timestamp = timestamp;
  neighbour->heard_timestamp = getRTCClock()->getCurrentTime();
  neighbour->snr = (int8_t)(snr * 4);
#endif
}

uint8_t MyMesh::handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood) {
  ClientInfo* client = NULL;
  if (data[0] == 0) {   // blank password, just check if sender is in ACL
    client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    if (client == NULL) {
    #if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Login, sender not in ACL");
    #endif
    }
  }
  if (client == NULL) {
    uint8_t perms;
    if (strcmp((char *)data, _prefs.password) == 0) { // check for valid admin password
      perms = PERM_ACL_ADMIN;
    } else if (strcmp((char *)data, _prefs.guest_password) == 0) { // check guest password
      perms = PERM_ACL_GUEST;
    } else {
#if MESH_DEBUG
      MESH_DEBUG_PRINTLN("Invalid password: %s", data);
#endif
      return 0;
    }

    client = acl.putClient(sender, 0);  // add to contacts (if not already known)
    if (sender_timestamp <= client->last_timestamp) {
      MESH_DEBUG_PRINTLN("Possible login replay attack!");
      return 0;  // FATAL: client table is full -OR- replay attack
    }

    MESH_DEBUG_PRINTLN("Login success!");
    client->last_timestamp = sender_timestamp;
    client->last_activity = getRTCClock()->getCurrentTime();
    client->permissions &= ~0x03;
    client->permissions |= perms;
    memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

    if (perms != PERM_ACL_GUEST) {   // keep number of FS writes to a minimum
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    }
  }

  if (is_flood) {
    client->out_path_len = OUT_PATH_UNKNOWN;  // need to rediscover out_path
  }

  if (client->isAdmin()) stopSleepCycle();   // an admin login ends a running sleep cycle

  uint32_t now = getRTCClock()->getCurrentTimeUnique();
  memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  reply_data[4] = RESP_SERVER_LOGIN_OK;
  reply_data[5] = 0;  // Legacy: was recommended keep-alive interval (secs / 16)
  reply_data[6] = client->isAdmin() ? 1 : 0;
  reply_data[7] = client->permissions;
  getRNG()->random(&reply_data[8], 4);   // random blob to help packet-hash uniqueness
  reply_data[12] = FIRMWARE_VER_LEVEL;  // New field

  return 13;  // reply length
}

uint8_t MyMesh::handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)

    return 8 + region_map.exportNamesTo((char *) &reply_data[8], sizeof(reply_data) - 12, REGION_DENY_FLOOD);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    sprintf((char *) &reply_data[8], "%s\n%s", _prefs.node_name, _prefs.owner_info);

    return 8 + strlen((char *) &reply_data[8]);   // reply length
  }
  return 0;
}

uint8_t MyMesh::handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data) {
  if (anon_limiter.allow(rtc_clock.getCurrentTime())) {
    // request data has: {reply-path-len}{reply-path}
    reply_path_len = *data++;
    if (!mesh::Packet::isValidPathLen(reply_path_len)) return 0;  // reject - bad encoding

    mesh::Packet::writePath(reply_path, data, reply_path_len);
    // data += (uint8_t)reply_path_len * reply_path_hash_size;

    memcpy(reply_data, &sender_timestamp, 4);   // prefix with sender_timestamp, like a tag
    uint32_t now = getRTCClock()->getCurrentTime();
    memcpy(&reply_data[4], &now, 4);     // include our clock (for easy clock sync, and packet hash uniqueness)
    reply_data[8] = 0;  // features
#ifdef WITH_RS232_BRIDGE
    reply_data[8] |= 0x01;  // is bridge, type UART
#elif WITH_ESPNOW_BRIDGE
    reply_data[8] |= 0x03;  // is bridge, type ESP-NOW
#endif
    if (_prefs.disable_fwd) {   // is this repeater currently disabled
      reply_data[8] |= 0x80;  // is disabled
    }
    // TODO:  add some kind of moving-window utilisation metric, so can query 'how busy' is this repeater
    return 9;   // reply length
  }
  return 0;
}

int MyMesh::handleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len) {
  // uint32_t now = getRTCClock()->getCurrentTimeUnique();
  // memcpy(reply_data, &now, 4);   // response packets always prefixed with timestamp
  memcpy(reply_data, &sender_timestamp, 4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

  if (payload[0] == REQ_TYPE_GET_STATUS) {  // guests can also access this now
    RepeaterStats stats;
    stats.batt_milli_volts = board.getBattMilliVolts();
    stats.curr_tx_queue_len = _mgr->getOutboundTotal();
    stats.noise_floor = (int16_t)_radio->getNoiseFloor();
    stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
    stats.n_packets_recv = radio_driver.getPacketsRecv();
    stats.n_packets_sent = radio_driver.getPacketsSent();
    stats.total_air_time_secs = getTotalAirTime() / 1000;
    stats.total_up_time_secs = uptime_millis / 1000;
    stats.n_sent_flood = getNumSentFlood();
    stats.n_sent_direct = getNumSentDirect();
    stats.n_recv_flood = getNumRecvFlood();
    stats.n_recv_direct = getNumRecvDirect();
    stats.err_events = _err_flags;
    stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
    stats.n_direct_dups = ((SimpleMeshTables *)getTables())->getNumDirectDups();
    stats.n_flood_dups = ((SimpleMeshTables *)getTables())->getNumFloodDups();
    stats.total_rx_air_time_secs = getReceiveAirTime() / 1000;
    stats.n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&reply_data[4], &stats, sizeof(stats));

    return 4 + sizeof(stats); //  reply_len
  }
  if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t perm_mask = ~(payload[1]); // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions

    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);

    // query other sensors -- target specific
    if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
      perm_mask = 0x00;  // just base telemetry allowed
    }
    sensors.querySensors(perm_mask, telemetry);

	// This default temperature will be overridden by external sensors (if any)
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    uint8_t tlen = telemetry.getSize();
    memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
    return 4 + tlen; // reply_len
  }
  if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin()) {
    uint8_t res1 = payload[1];   // reserved for future  (extra query params)
    uint8_t res2 = payload[2];
    if (res1 == 0 && res2 == 0) {
      uint8_t ofs = 4;
      for (int i = 0; i < acl.getNumClients() && ofs + 7 <= sizeof(reply_data) - 4; i++) {
        auto c = acl.getClientByIdx(i);
        if (c->permissions == 0) continue;  // skip deleted entries
        memcpy(&reply_data[ofs], c->id.pub_key, 6); ofs += 6;  // just 6-byte pub_key prefix
        reply_data[ofs++] = c->permissions;
      }
      return ofs;
    }
  }
  if (payload[0] == REQ_TYPE_GET_NEIGHBOURS) {
    uint8_t request_version = payload[1];
    if (request_version == 0) {

      // reply data offset (after response sender_timestamp/tag)
      int reply_offset = 4;

      // get request params
      uint8_t count = payload[2]; // how many neighbours to fetch (0-255)
      uint16_t offset;
      memcpy(&offset, &payload[3], 2); // offset from start of neighbours list (0-65535)
      uint8_t order_by = payload[5]; // how to order neighbours. 0=newest_to_oldest, 1=oldest_to_newest, 2=strongest_to_weakest, 3=weakest_to_strongest
      uint8_t pubkey_prefix_length = payload[6]; // how many bytes of neighbour pub key we want
      // we also send a 4 byte random blob in payload[7...10] to help packet uniqueness

      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS count=%d, offset=%d, order_by=%d, pubkey_prefix_length=%d", count, offset, order_by, pubkey_prefix_length);

      // clamp pub key prefix length to max pub key length
      if(pubkey_prefix_length > PUB_KEY_SIZE){
        pubkey_prefix_length = PUB_KEY_SIZE;
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS invalid pubkey_prefix_length=%d clamping to %d", pubkey_prefix_length, PUB_KEY_SIZE);
      }

      // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
      int16_t neighbours_count = 0;
#if MAX_NEIGHBOURS
      NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
      for (int i = 0; i < MAX_NEIGHBOURS; i++) {
        auto neighbour = &neighbours[i];
        if (neighbour->heard_timestamp > 0) {
          sorted_neighbours[neighbours_count] = neighbour;
          neighbours_count++;
        }
      }

      // sort neighbours based on order
      if (order_by == 0) {
        // sort by newest to oldest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting newest to oldest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp > b->heard_timestamp; // desc
        });
      } else if (order_by == 1) {
        // sort by oldest to newest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting oldest to newest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->heard_timestamp < b->heard_timestamp; // asc
        });
      } else if (order_by == 2) {
        // sort by strongest to weakest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting strongest to weakest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr > b->snr; // desc
        });
      } else if (order_by == 3) {
        // sort by weakest to strongest
        MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS sorting weakest to strongest");
        std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
          return a->snr < b->snr; // asc
        });
      }
#endif

      // build results buffer
      int results_count = 0;
      int results_offset = 0;
      uint8_t results_buffer[130];
      for(int index = 0; index < count && index + offset < neighbours_count; index++){
        
        // stop if we can't fit another entry in results
        int entry_size = pubkey_prefix_length + 4 + 1;
        if(results_offset + entry_size > sizeof(results_buffer)){
          MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS no more entries can fit in results buffer");
          break;
        }

#if MAX_NEIGHBOURS
        // add next neighbour to results
        auto neighbour = sorted_neighbours[index + offset];
        uint32_t heard_seconds_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
        memcpy(&results_buffer[results_offset], neighbour->id.pub_key, pubkey_prefix_length); results_offset += pubkey_prefix_length;
        memcpy(&results_buffer[results_offset], &heard_seconds_ago, 4); results_offset += 4;
        memcpy(&results_buffer[results_offset], &neighbour->snr, 1); results_offset += 1;
        results_count++;
#endif

      }

      // build reply
      MESH_DEBUG_PRINTLN("REQ_TYPE_GET_NEIGHBOURS neighbours_count=%d results_count=%d", neighbours_count, results_count);
      memcpy(&reply_data[reply_offset], &neighbours_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_count, 2); reply_offset += 2;
      memcpy(&reply_data[reply_offset], &results_buffer, results_offset); reply_offset += results_offset;

      return reply_offset;
    }
  } else if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
    sprintf((char *) &reply_data[4], "%s\n%s\n%s", FIRMWARE_VERSION, _prefs.node_name, _prefs.owner_info);
    return 4 + strlen((char *) &reply_data[4]);
  }
  return 0; // unknown command
}

mesh::Packet *MyMesh::createSelfAdvert() {
  uint8_t app_data[MAX_ADVERT_DATA_SIZE];
  uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_REPEATER, app_data);

  return createAdvert(self_id, app_data, app_data_len);
}

File MyMesh::openAppend(const char *fname) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return _fs->open(fname, "a");
#else
  return _fs->open(fname, "a", true);
#endif
}

static uint8_t max_loop_minimal[] =  { 0, /* 1-byte */  4, /* 2-byte */  2, /* 3-byte */  1 };
static uint8_t max_loop_moderate[] = { 0, /* 1-byte */  2, /* 2-byte */  1, /* 3-byte */  1 };
static uint8_t max_loop_strict[] =   { 0, /* 1-byte */  1, /* 2-byte */  1, /* 3-byte */  1 };

bool MyMesh::isLooped(const mesh::Packet* packet, const uint8_t max_counters[]) {
  uint8_t hash_size = packet->getPathHashSize();
  uint8_t hash_count = packet->getPathHashCount();
  uint8_t n = 0;
  const uint8_t* path = packet->path;
  while (hash_count > 0) {      // count how many times this node is already in the path
    if (self_id.isHashMatch(path, hash_size)) n++;
    hash_count--;
    path += hash_size;
  }
  return n >= max_counters[hash_size];
}

void MyMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size) {
  TransportKey req_scope;
  bool is_wildcard = recv_pkt_region != NULL && recv_pkt_region->isWildcard();
  bool req_scope_known = recv_pkt_region != NULL && !is_wildcard
                      && region_map.getTransportKeysFor(*recv_pkt_region, &req_scope, 1) > 0;

  switch (mesh::chooseReplyScope(req_scope_known, is_wildcard, !default_scope.isNull())) {
    case mesh::REPLY_SCOPE_REQUEST:
      sendFloodScoped(req_scope, packet, delay_millis, path_hash_size);   // reply with same scope as request
      break;
    case mesh::REPLY_SCOPE_DEFAULT:
      // requester's scope is unknown: DIRECT request (no transport codes), or code matched no Region.
      // un-scoped would be dropped at hop 0 by repeaters running flood.max.unscoped=0
      sendFloodScoped(default_scope, packet, delay_millis, path_hash_size);
      break;
    case mesh::REPLY_SCOPE_NONE:
      sendFlood(packet, delay_millis, path_hash_size);  // send un-scoped
      break;
  }
}

bool MyMesh::allowPacketForward(const mesh::Packet *packet) {
  if (_prefs.disable_fwd) return false;
  if (packet->isRouteFlood() && blockHopCheck(packet)) return false;   // first relay is on the blockhop list
  if (packet->isRouteFlood()
      && mesh::isFloodHopLimitExceeded(packet, _prefs.flood_max, _prefs.flood_max_unscoped, _prefs.flood_max_advert)) {
    return false;
  }
  if (packet->isRouteFlood() && recv_pkt_region == NULL) {
    MESH_DEBUG_PRINTLN("allowPacketForward: unknown transport code, or wildcard not allowed for FLOOD packet");
    return false;
  }
  if (packet->isRouteFlood() && _prefs.loop_detect != LOOP_DETECT_OFF) {
    const uint8_t* maximums;
    if (_prefs.loop_detect == LOOP_DETECT_MINIMAL) {
      maximums = max_loop_minimal;
    } else if (_prefs.loop_detect == LOOP_DETECT_MODERATE) {
      maximums = max_loop_moderate;
    } else {
      maximums = max_loop_strict;
    }
    if (isLooped(packet, maximums)) {
      MESH_DEBUG_PRINTLN("allowPacketForward: FLOOD packet loop detected!");
      return false;
    }
  }
  return true;
}

const char *MyMesh::getLogDateTime() {
  static char tmp[32];
  uint32_t now = getRTCClock()->getCurrentTime();
  DateTime dt = DateTime(now);
  sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U", dt.hour(), dt.minute(), dt.second(), dt.day(), dt.month(),
          dt.year());
  return tmp;
}

void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
  Serial.print(getLogDateTime());
  Serial.print(" RAW: ");
  mesh::Utils::printHex(Serial, raw, len);
  Serial.println();
#endif
}

void MyMesh::logRx(mesh::Packet *pkt, int len, float score) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 1) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d", len,
               pkt->getPayloadType(), pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
               (int)_radio->getLastSNR(), (int)_radio->getLastRSSI(), (int)(score * 1000));

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTx(mesh::Packet *pkt, int len) {
#ifdef WITH_BRIDGE
  if (_prefs.bridge_pkt_src == 0) {
    bridge.sendPacket(pkt);
  }
#endif

  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);

      if (pkt->getPayloadType() == PAYLOAD_TYPE_PATH || pkt->getPayloadType() == PAYLOAD_TYPE_REQ ||
          pkt->getPayloadType() == PAYLOAD_TYPE_RESPONSE || pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
        f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
      } else {
        f.printf("\n");
      }
      f.close();
    }
  }
}

void MyMesh::logTxFail(mesh::Packet *pkt, int len) {
  if (_logging) {
    File f = openAppend(PACKET_LOG_FILE);
    if (f) {
      f.print(getLogDateTime());
      f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n", len, pkt->getPayloadType(),
               pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
      f.close();
    }
  }
}

int MyMesh::calcRxDelay(float score, uint32_t air_time) const {
  if (_prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t MyMesh::getRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}
uint32_t MyMesh::getDirectRetransmitDelay(const mesh::Packet *packet) {
  uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
  return getRNG()->nextInt(0, 5*t + 1);
}

mesh::DispatcherAction MyMesh::onRecvPacket(mesh::Packet* pkt) {
  if (pkt->getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
    recv_pkt_region = region_map.findMatch(pkt, REGION_DENY_FLOOD);
  } else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
    if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
      recv_pkt_region = NULL;
    } else {
      recv_pkt_region =  &region_map.getWildcard();
    }
  } else {
    recv_pkt_region = NULL;
  }
  return Mesh::onRecvPacket(pkt);
}

void MyMesh::onAnonDataRecv(mesh::Packet *packet, const uint8_t *secret, const mesh::Identity &sender,
                            uint8_t *data, size_t len) {
  if (packet->getPayloadType() == PAYLOAD_TYPE_ANON_REQ) { // received an initial request by a possible admin
                                                           // client (unknown at this stage)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    data[len] = 0;  // ensure null terminator
    uint8_t reply_len;

    reply_path_len = 0xFF;
    if (data[4] == 0 || data[4] >= ' ') {   // is password, ie. a login request
      reply_len = handleLoginReq(sender, secret, timestamp, &data[4], packet->isRouteFlood());
    } else if (data[4] == ANON_REQ_TYPE_REGIONS && packet->isRouteDirect()) {
      reply_len = handleAnonRegionsReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_OWNER && packet->isRouteDirect()) {
      reply_len = handleAnonOwnerReq(sender, timestamp, &data[5]);
    } else if (data[4] == ANON_REQ_TYPE_BASIC && packet->isRouteDirect()) {
      reply_len = handleAnonClockReq(sender, timestamp, &data[5]);
    } else {
      reply_len = 0;  // unknown/invalid request type
    }

    if (reply_len == 0) return;   // invalid request

    // a DIRECT login can reply via the stored out_path, as onPeerDataRecv() does for REQ
    ClientInfo* client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    bool have_out_path = client != NULL && client->out_path_len != OUT_PATH_UNKNOWN;

    auto route = mesh::chooseReplyRoute(packet->isRouteFlood(), reply_path_len != 0xFF, have_out_path);

    if (route == mesh::REPLY_ROUTE_PATH_RETURN) {
      // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
      mesh::Packet* path = createPathReturn(sender, secret, packet->path, packet->path_len,
                                            PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
      if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      return;
    }

    mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender, secret, reply_data, reply_len);
    if (reply == NULL) return;

    if (route == mesh::REPLY_ROUTE_DIRECT_SUPPLIED) {
      sendDirect(reply, reply_path, reply_path_len, SERVER_RESPONSE_DELAY);
    } else if (route == mesh::REPLY_ROUTE_DIRECT_OUT_PATH) {
      sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
    } else {
      sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    }
  }
}

int MyMesh::searchPeersByHash(const uint8_t *hash) {
  int n = 0;
  for (int i = 0; i < acl.getNumClients(); i++) {
    if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
      matching_peer_indexes[n++] = i; // store the INDEXES of matching contacts (for subsequent 'peer' methods)
    }
  }
  return n;
}

void MyMesh::getPeerSharedSecret(uint8_t *dest_secret, int peer_idx) {
  int i = matching_peer_indexes[peer_idx];
  if (i >= 0 && i < acl.getNumClients()) {
    // lookup pre-calculated shared_secret
    memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
  } else {
    MESH_DEBUG_PRINTLN("getPeerSharedSecret: Invalid peer idx: %d", i);
  }
}

static bool isShare(const mesh::Packet *packet) {
  if (packet->hasTransportCodes()) {
    return packet->transport_codes[0] == 0 && packet->transport_codes[1] == 0;  // codes { 0, 0 } means 'send to nowhere'
  }
  return false;
}

void MyMesh::onAdvertRecv(mesh::Packet *packet, const mesh::Identity &id, uint32_t timestamp,
                          const uint8_t *app_data, size_t app_data_len) {
  mesh::Mesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len); // chain to super impl

  // blockhop auto-add: a repeater advertising a location in the northern hemisphere (we are in Brisbane)
  {
    AdvertDataParser loc_parser(app_data, app_data_len);
    if (loc_parser.isValid() && loc_parser.getType() == ADV_TYPE_REPEATER
        && loc_parser.hasLatLon() && loc_parser.getIntLat() > 0) {
      blockHopAdd(id.pub_key, 3, true);
    }
  }

  // if this a zero hop advert (and not via 'Share'), add it to neighbours
  if (packet->getPathHashCount() == 0 && !isShare(packet)) {
    AdvertDataParser parser(app_data, app_data_len);
    if (parser.isValid() && parser.getType() == ADV_TYPE_REPEATER) { // just keep neigbouring Repeaters
      putNeighbour(id, timestamp, packet->getSNR());
    }
  }
}

void MyMesh::onPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx, const uint8_t *secret,
                            uint8_t *data, size_t len) {
  int i = matching_peer_indexes[sender_idx];
  if (i < 0 || i >= acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("onPeerDataRecv: invalid peer idx: %d", i);
    return;
  }
  ClientInfo* client = acl.getClientByIdx(i);

  if (type == PAYLOAD_TYPE_REQ) { // request (from a Known admin client!)
    uint32_t timestamp;
    memcpy(&timestamp, data, 4);

    if (timestamp > client->last_timestamp) { // prevent replay attacks
      int reply_len = handleRequest(client, timestamp, &data[4], len - 4);
      if (reply_len == 0) return; // invalid command

      client->last_timestamp = timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      if (packet->isRouteFlood()) {
        // let this sender know path TO here, so they can use sendDirect(), and ALSO encode the response
        mesh::Packet *path = createPathReturn(client->id, secret, packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, reply_data, reply_len);
        if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
      } else {
        mesh::Packet *reply =
            createDatagram(PAYLOAD_TYPE_RESPONSE, client->id, secret, reply_data, reply_len);
        if (reply) {
          if (client->out_path_len != OUT_PATH_UNKNOWN) { // we have an out_path, so send DIRECT
            sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
          } else {
            sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  } else if (type == PAYLOAD_TYPE_TXT_MSG && len > 5 && client->isAdmin()) { // a CLI command
    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4); // timestamp (by sender's RTC clock - which could be wrong)
    uint8_t flags = (data[4] >> 2);        // message attempt number, and other flags

    if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA)) {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: unsupported text type received: flags=%02x", (uint32_t)flags);
    } else if (sender_timestamp >= client->last_timestamp) { // prevent replay attacks
      bool is_retry = (sender_timestamp == client->last_timestamp);
      client->last_timestamp = sender_timestamp;
      client->last_activity = getRTCClock()->getCurrentTime();

      // len can be > original length, but 'text' will be padded with zeroes
      data[len] = 0; // need to make a C string again, with null terminator

      if (flags == TXT_TYPE_PLAIN) { // for legacy CLI, send Acks
        uint32_t ack_hash; // calc truncated hash of the message timestamp + text + sender pub_key, to prove
                           // to sender that we got it
        mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]), client->id.pub_key,
                            PUB_KEY_SIZE);

        mesh::Packet *ack = createAck(ack_hash);
        if (ack) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
          } else {
            sendDirect(ack, client->out_path, client->out_path_len, TXT_ACK_DELAY);
          }
        }
      }

      uint8_t temp[166];
      char *command = (char *)&data[5];
      char *reply = (char *)&temp[5];
      if (is_retry) {
        *reply = 0;
      } else {
        handleCommand(sender_timestamp, command, reply);
      }
      int text_len = strlen(reply);
      if (text_len > 0) {
        uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
        if (timestamp == sender_timestamp) {
          // WORKAROUND: the two timestamps need to be different, in the CLI view
          timestamp++;
        }
        memcpy(temp, &timestamp, 4);        // mostly an extra blob to help make packet_hash unique
        temp[4] = (TXT_TYPE_CLI_DATA << 2); // NOTE: legacy was: TXT_TYPE_PLAIN

        auto reply = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, temp, 5 + text_len);
        if (reply) {
          if (client->out_path_len == OUT_PATH_UNKNOWN) {
            sendFloodReply(reply, CLI_REPLY_DELAY_MILLIS, packet->getPathHashSize());
          } else {
            sendDirect(reply, client->out_path, client->out_path_len, CLI_REPLY_DELAY_MILLIS);
          }
        }
      }
    } else {
      MESH_DEBUG_PRINTLN("onPeerDataRecv: possible replay attack detected");
    }
  }
}

bool MyMesh::onPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret, uint8_t *path,
                            uint8_t path_len, uint8_t extra_type, uint8_t *extra, uint8_t extra_len) {
  // TODO: prevent replay attacks
  int i = matching_peer_indexes[sender_idx];

  if (i >= 0 && i < acl.getNumClients()) { // get from our known_clients table (sender SHOULD already be known in this context)
    MESH_DEBUG_PRINTLN("PATH to client, path_len=%d", (uint32_t)path_len);
    auto client = acl.getClientByIdx(i);

    // store a copy of path, for sendDirect()
    client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
    client->last_activity = getRTCClock()->getCurrentTime();
  } else {
    MESH_DEBUG_PRINTLN("onPeerPathRecv: invalid peer idx: %d", i);
  }

  // NOTE: no reciprocal path send!!
  return false;
}

#define CTL_TYPE_NODE_DISCOVER_REQ   0x80
#define CTL_TYPE_NODE_DISCOVER_RESP  0x90

void MyMesh::onControlDataRecv(mesh::Packet* packet) {
  uint8_t type = packet->payload[0] & 0xF0;    // just test upper 4 bits
  if (type == CTL_TYPE_NODE_DISCOVER_REQ && packet->payload_len >= 6
      && !_prefs.disable_fwd && discover_limiter.allow(rtc_clock.getCurrentTime())
  ) {
    int i = 1;
    uint8_t  filter = packet->payload[i++];
    uint32_t tag;
    memcpy(&tag, &packet->payload[i], 4); i += 4;
    uint32_t since;
    if (packet->payload_len >= i+4) {   // optional since field
      memcpy(&since, &packet->payload[i], 4); i += 4;
    } else {
      since = 0;
    }

    if ((filter & (1 << ADV_TYPE_REPEATER)) != 0 && _prefs.discovery_mod_timestamp >= since) {
      bool prefix_only = packet->payload[0] & 1;
      uint8_t data[6 + PUB_KEY_SIZE];
      data[0] = CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER;   // low 4-bits for node type
      data[1] = packet->_snr;   // let sender know the inbound SNR ( x 4)
      memcpy(&data[2], &tag, 4);     // include tag from request, for client to match to
      memcpy(&data[6], self_id.pub_key, PUB_KEY_SIZE);
      auto resp = createControlData(data, prefix_only ? 6 + 8 : 6 + PUB_KEY_SIZE);
      if (resp) {
        sendZeroHop(resp, getRetransmitDelay(resp)*4);  // apply random delay (widened x4), as multiple nodes can respond to this
      }
    }
  } else if (type == CTL_TYPE_NODE_DISCOVER_RESP && packet->payload_len >= 6) {
    uint8_t node_type = packet->payload[0] & 0x0F;
    if (node_type != ADV_TYPE_REPEATER) {
      return;
    }
    if (packet->payload_len < 6 + PUB_KEY_SIZE) {
      MESH_DEBUG_PRINTLN("onControlDataRecv: DISCOVER_RESP pubkey too short: %d", (uint32_t)packet->payload_len);
      return;
    }

    if (pending_discover_tag == 0 || millisHasNowPassed(pending_discover_until)) {
      pending_discover_tag = 0;
      return;
    }
    uint32_t tag;
    memcpy(&tag, &packet->payload[2], 4);
    if (tag != pending_discover_tag) {
      return;
    }

    mesh::Identity id(&packet->payload[6]);
    if (id.matches(self_id)) {
      return;
    }
    putNeighbour(id, rtc_clock.getCurrentTime(), packet->getSNR());
  }
}

void MyMesh::sendNodeDiscoverReq() {
  uint8_t data[10];
  data[0] = CTL_TYPE_NODE_DISCOVER_REQ; // prefix_only=0
  data[1] = (1 << ADV_TYPE_REPEATER);
  getRNG()->random(&data[2], 4); // tag
  memcpy(&pending_discover_tag, &data[2], 4);
  pending_discover_until = futureMillis(60000);
  uint32_t since = 0;
  memcpy(&data[6], &since, 4);

  auto pkt = createControlData(data, sizeof(data));
  if (pkt) {
    sendZeroHop(pkt);
  }
}

MyMesh::MyMesh(mesh::MainBoard &board, mesh::Radio &radio, mesh::MillisecondClock &ms, mesh::RNG &rng,
               mesh::RTCClock &rtc, mesh::MeshTables &tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this),
      telemetry(MAX_PACKET_PAYLOAD - 4),
      discover_limiter(4, 120),  // max 4 every 2 minutes
      anon_limiter(4, 180)   // max 4 every 3 minutes
#if defined(WITH_RS232_BRIDGE)
      , bridge(&_prefs, WITH_RS232_BRIDGE, _mgr, &rtc)
#endif
#if defined(WITH_ESPNOW_BRIDGE)
      , bridge(&_prefs, _mgr, &rtc)
#endif
{
  last_millis = 0;
  uptime_millis = 0;
  next_local_advert = next_flood_advert = 0;
  dirty_contacts_expiry = 0;
  set_radio_at = revert_radio_at = 0;
  sleep_pending = sleeping = false;
  sleep_start_at = sleep_wake_at = 0;
  sleep_duration_ms = 0;
  cycle_active = false;
  sleep_auto_enabled = true;   // low-voltage auto-start is ON after every boot
  cycle_awake_until = 0;
  batt_check_armed = false;
  next_batt_check = 0;
  batt_low_count = batt_crit_count = 0;
  batt_check_n = 0;
  batt_log_head = batt_log_count = 0;
  memset(temp_log, 0, sizeof(temp_log));
  memset(blockhop, 0, sizeof(blockhop));
  hot_count = 0;
  hot_msg_sent = crit_msg_sent = false;
  blk_pending_n = 0;
  blk_msg_armed = false;
  blk_next_msg = 0;
  sleep_deadline = 0;
  chan_key_set = false;
  chan_key_len = 0;
  memset(&chan, 0, sizeof(chan));
  chan_q_n = 0;
  chan_gap_on = false;
  chan_next_send_at = 0;
  report_armed = false;
  report_due_at = 0;
  last_cmd_ts = 0;
  from_channel = false;
  sleep_target_at = 0;
  sleep_target_abs = false;
  busy_armed = false;
  busy_until = 0;
  _logging = false;
  region_load_active = false;
  recv_pkt_region = NULL;

#if MAX_NEIGHBOURS
  memset(neighbours, 0, sizeof(neighbours));
#endif

  // defaults
  _prefs.airtime_factor = 1.0;
  _prefs.rx_delay_base = 0.0f;   // turn off by default, was 10.0;
  _prefs.tx_delay_factor = 0.5f; // was 0.25f
  _prefs.direct_tx_delay_factor = 0.3f; // was 0.2
  StrHelper::strncpy(_prefs.node_name, ADVERT_NAME, sizeof(_prefs.node_name));
  _prefs.node_lat = ADVERT_LAT;
  _prefs.node_lon = ADVERT_LON;
  StrHelper::strncpy(_prefs.password, ADMIN_PASSWORD, sizeof(_prefs.password));
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.advert_interval = 1;        // default to 2 minutes for NEW installs
  _prefs.flood_advert_interval = 47; // 47 hours
  _prefs.flood_max = 64;
  _prefs.flood_max_unscoped = 64;
  _prefs.flood_max_advert = 8;
  _prefs.interference_threshold = 0; // disabled
  _prefs.cad_enabled = 0;            // hardware CAD before TX (off by default; 'set cad on')

  // bridge defaults
  _prefs.bridge_enabled = 1;    // enabled
  _prefs.bridge_delay   = 500;  // milliseconds
  _prefs.bridge_pkt_src = 0;    // logTx
  _prefs.bridge_baud = 115200;  // baud rate
  _prefs.bridge_channel = 1;    // channel 1

  StrHelper::strncpy(_prefs.bridge_secret, "LVSITANOS", sizeof(_prefs.bridge_secret));

  // GPS defaults
  _prefs.gps_enabled = 0;
  _prefs.gps_interval = 0;
  _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

  _prefs.adc_multiplier = 0.0f; // 0.0f means use default board multiplier

#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default;
#endif
#endif
  _prefs.radio_fem_rxgain = 1;
  _prefs.radio_fem_txgain = 0;

  pending_discover_tag = 0;
  pending_discover_until = 0;

  memset(default_scope.key, 0, sizeof(default_scope.key));
}

void MyMesh::begin(FILESYSTEM *fs) {
  mesh::Mesh::begin();
  _fs = fs;
  // load persisted prefs
  _cli.loadPrefs(_fs);
  acl.load(_fs, self_id);
  // TODO: key_store.begin();
  region_map.load(_fs);
  loadChannelKey();   // channel key saved by 'channel set'

  // establish default-scope
  {
    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
      region_map.getTransportKeysFor(*r, &default_scope, 1);
    } else {
#ifdef DEFAULT_FLOOD_SCOPE_NAME
      r = region_map.findByName(DEFAULT_FLOOD_SCOPE_NAME);
      if (r == NULL) {
        r = region_map.putRegion(DEFAULT_FLOOD_SCOPE_NAME, 0);  // auto-create the default scope region
        if (r) { r->flags = 0; }   // Allow-flood
      }
      if (r) {
        region_map.setDefaultRegion(r);
        region_map.getTransportKeysFor(*r, &default_scope, 1);
      }
#endif
    }
  }

#if defined(WITH_BRIDGE)
  if (_prefs.bridge_enabled) {
    bridge.begin();
  }
#endif

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);

  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
  board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);
  board.setLoRaFemPaGainEnabled(_prefs.radio_fem_txgain);

  updateAdvertTimer();
  updateFloodAdvertTimer();

  board.setAdcMultiplier(_prefs.adc_multiplier);

#if ENV_INCLUDE_GPS == 1
  applyGpsPrefs();
#endif
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, path_hash_size);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, path_hash_size);
  }
}

void MyMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
  set_radio_at = futureMillis(2000); // give CLI reply some time to be sent back, before applying temp radio params
  pending_freq = freq;
  pending_bw = bw;
  pending_sf = sf;
  pending_cr = cr;

  revert_radio_at = futureMillis(2000 + timeout_mins * 60 * 1000); // schedule when to revert radio params
}

/* ------------------------- sleep / battery / blockhop ------------------------- */

static void fmtAge(char* out, size_t n, unsigned long ms) {
  unsigned long s = ms / 1000UL;
  if (s < 60) snprintf(out, n, "%lus", s);
  else if (s < 3600) snprintf(out, n, "%lum", s / 60);
  else if (s < 86400) snprintf(out, n, "%luh", s / 3600);
  else snprintf(out, n, "%lud", s / 86400);
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// parse 2, 4 or 6 hex chars into 1..3 bytes. returns number of bytes, or 0 if invalid
static int parseHexId(const char* s, uint8_t* key) {
  size_t n = strlen(s);
  if (n != 2 && n != 4 && n != 6) return 0;
  for (size_t i = 0; i < n; i += 2) {
    int hi = hexNibble(s[i]);
    int lo = hexNibble(s[i + 1]);
    if (hi < 0 || lo < 0) return 0;
    key[i / 2] = (uint8_t)((hi << 4) | lo);
  }
  return (int)(n / 2);
}

static int b64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// base64 -> bytes. returns number of bytes, or -1 if invalid
static int b64Decode(const char* in, uint8_t* out, int max_out) {
  int n = 0, bits = 0;
  uint32_t acc = 0;
  for (; *in; in++) {
    if (*in == '=') break;
    int v = b64Val(*in);
    if (v < 0) return -1;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= max_out) return -1;
      out[n++] = (uint8_t)((acc >> bits) & 0xFF);
    }
  }
  return n;
}

// bytes -> base64 (with padding). out must hold 4*((n+2)/3)+1 bytes
static void b64Encode(const uint8_t* in, int n, char* out) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = tbl[(v >> 18) & 63];
    out[o++] = tbl[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? tbl[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? tbl[v & 63] : '=';
  }
  out[o] = 0;
}

// drop the oldest values from a log line ("V/h old>new: 4.05 4.08 ...") until prefix + line fits one message
static void trimLogToFit(char* buf, int hdr_len, int prefix_len) {
  while ((int)strlen(buf) + prefix_len > CHAN_MSG_MAX) {
    char* p = strchr(buf + hdr_len + 1, ' ');
    if (p == NULL) break;
    memmove(buf + hdr_len, p, strlen(p) + 1);
  }
}

static int lowerc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

// true if s ends with " <target>" (case-insensitive). cmd_len = length of the command in front of it
static bool endsWithTarget(const char* s, int slen, const char* target, int* cmd_len) {
  int tl = (int)strlen(target);
  if (tl == 0 || slen < tl + 2) return false;
  if (s[slen - tl - 1] != ' ') return false;
  for (int i = 0; i < tl; i++) {
    if (lowerc(s[slen - tl + i]) != lowerc(target[i])) return false;
  }
  *cmd_len = slen - tl - 1;
  return true;
}

#ifdef HELTEC_T096
// Heltec T096: switch the LoRa front-end chip (KCT8103L) fully off while the radio sleeps.
static void t096FrontEndOff() {
  board.loRaFEMControl.setSleepModeEnable();      // front-end shutdown pin low first
  nrf_gpio_cfg_default(P_LORA_KCT8103L_PA_CSD);   // release control pins (no leakage into an unpowered chip)
  nrf_gpio_cfg_default(P_LORA_KCT8103L_PA_CTX);
  digitalWrite(P_LORA_PA_POWER, LOW);             // cut the front-end supply
#ifdef PIN_GPS_EN
  nrf_gpio_cfg_default(PIN_GPS_EN);               // same as T096Board::powerOff(): GPS enable pin ~363uA -> ~39uA
#endif
}

static void t096FrontEndOn() {
#if ENV_INCLUDE_GPS == 1
  pinMode(PIN_GPS_EN, OUTPUT);
  digitalWrite(PIN_GPS_EN, !PIN_GPS_EN_ACTIVE);   // GPS stays off (same state as T096Board::initiateShutdown)
#endif                                            // (without GPS code the pin stays released = lowest draw)
  board.loRaFEMControl.init();                    // supply on, 1ms, enable, RX mode - same as at boot
}
#endif

void MyMesh::startSleep(uint32_t duration_ms, bool absolute) {
  sleep_duration_ms = duration_ms;
  sleep_target_abs = absolute;                    // 'sleep until': wake time counts from the command, not from when the radio goes off
  sleep_target_at = futureMillis(duration_ms);
  sleep_start_at = futureMillis(3000);  // give CLI reply some time to be sent back, before the radio goes off
  sleep_deadline = futureMillis(3000 + SLEEP_MSG_WAIT_MS);   // ... and queued channel messages some time to go out
  sleep_pending = true;
}

bool MyMesh::sleepFor(uint32_t duration_ms) {   // (CommonCLI hook; 'sleep <mins>' is handled in handleSleepCommands)
  cycle_active = false;   // a manual sleep overrides a running cycle
  startSleep(duration_ms);
  return true;
}

void MyMesh::radioSleep(uint32_t duration_ms) {
  sleeping = true;
  sleep_wake_at = futureMillis(duration_ms);
  radio_driver.powerOff();
#ifdef HELTEC_T096
  t096FrontEndOff();
#endif
}

void MyMesh::radioWake() {
  sleeping = false;
  uint32_t saved_time = getRTCClock()->getCurrentTime();
#ifdef HELTEC_T096
  t096FrontEndOn();                   // front-end chip must be powered before the radio restarts
#endif
  radio_init();                       // full radio re-init (hardware reset), same as at boot
  getRTCClock()->setCurrentTime(saved_time);  // radio_init() may touch the clock; keep the time
  radio_driver.begin();               // re-arm receive state
  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);
  radio_driver.setCADEnabled(_prefs.cad_enabled);
}

void MyMesh::stopSleepCycle() {
  if (!cycle_active) return;
  if (sleep_pending) sleep_pending = false;   // cycle's first sleep hasn't started yet: cancel it
  cycle_active = false;
  batt_low_count = 0;   // auto-start needs 3 fresh low readings
}

void MyMesh::batteryMonitor() {
  char msg[CHAN_MSG_MAX + 1];

  // chip temperature warning
  float temp_c = board.getMCUTemperature();   // NAN on boards without a sensor
  if (!isnan(temp_c)) {
    if (temp_c >= HOT_START_C) {
      if (hot_count < 255) hot_count++;
    } else {
      hot_count = 0;
    }
    if (temp_c < HOT_RESUME_C) hot_msg_sent = false;   // re-armed
    if (hot_count >= HOT_COUNT && !hot_msg_sent) {
      hot_msg_sent = true;
      int t10 = (int)(temp_c * 10.0f + 0.5f);
      snprintf(msg, sizeof(msg), "Chip temp %d.%dC", t10 / 10, t10 % 10);
      chanEnqueue(msg, CHAN_GAP_MS);
    }
  }

  uint16_t mv = board.getBattMilliVolts();
  if (mv < 1000) return;   // implausible reading (e.g. no battery): ignore

  if ((batt_check_n % 12) == 0) {   // hourly log (every 12th check)
    batt_log[batt_log_head] = mv;
    float t = board.getMCUTemperature();   // chip temperature (NAN on boards without a sensor)
    temp_log[batt_log_head] = isnan(t) ? INT16_MIN : (int16_t)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f));
    batt_log_head = (batt_log_head + 1) % 24;
    if (batt_log_count < 24) batt_log_count++;
  }
  batt_check_n++;

  // 'battery critical' channel warning (best effort: needs the key set and an awake radio)
  if (mv < CRIT_WARN_MV) {
    if (!crit_msg_sent) {
      crit_msg_sent = true;
      snprintf(msg, sizeof(msg), "Battery critical %u.%02uV, shutting down until recharged",
               (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
      chanEnqueue(msg, CHAN_GAP_MS);
    }
  } else if (mv >= BATT_LOW_MV) {
    crit_msg_sent = false;   // re-armed
  }

  // last resort: reboot so the board's boot lock (below 3.3V) puts the node into deep sleep
#ifdef NRF52_POWER_MANAGEMENT   // (only boards with the nRF52 boot lock; elsewhere a reboot would not help)
  if (mv < BATT_CRIT_MV) {
    if (++batt_crit_count >= BATT_CRIT_COUNT) board.reboot();   // doesn't return
  } else {
    batt_crit_count = 0;
  }
#endif

  // low battery: start the sleep cycle
  if (mv < BATT_LOW_MV) {
    if (batt_low_count < 255) batt_low_count++;
  } else {
    batt_low_count = 0;
  }
  if (sleep_auto_enabled && batt_low_count >= BATT_LOW_COUNT && !cycle_active && !sleeping && !sleep_pending) {
    cycle_active = true;
    snprintf(msg, sizeof(msg), "Battery low %u.%02uV, sleep cycle started",
             (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
    chanEnqueue(msg, CHAN_GAP_MS);
    startSleep(CYCLE_SLEEP_MS);
  }
}

void MyMesh::formatBattLog(char* reply) {
  if (batt_log_count == 0) {
    strcpy(reply, "battlog: no data yet (first reading 5 min after boot)");
    return;
  }
  int pos = snprintf(reply, 20, "V/h old>new:");
  for (int i = 0; i < batt_log_count; i++) {
    int idx = (batt_log_head + 24 - batt_log_count + i) % 24;
    unsigned mv = batt_log[idx];
    pos += snprintf(&reply[pos], 8, " %u.%02u", mv / 1000, (mv % 1000) / 10);
  }
}

void MyMesh::formatTempLog(char* reply) {
  if (batt_log_count == 0) {
    strcpy(reply, "templog: no data yet (first reading 5 min after boot)");
    return;
  }
  int pos = snprintf(reply, 20, "C/h old>new:");
  for (int i = 0; i < batt_log_count; i++) {
    int idx = (batt_log_head + 24 - batt_log_count + i) % 24;
    int v = temp_log[idx];
    if (pos > CLI_REPLY_MAX - 10) break;   // never overrun the reply buffer
    if (temp_log[idx] == INT16_MIN) {
      pos += snprintf(&reply[pos], 4, " ?");
    } else {
      int a = v < 0 ? -v : v;
      pos += snprintf(&reply[pos], 10, " %s%d.%d", v < 0 ? "-" : "", a / 10, a % 10);
    }
  }
}

bool MyMesh::blockHopCheck(const mesh::Packet* packet) {
  uint8_t hs = packet->getPathHashSize();
  if (hs == 0 || packet->getPathHashCount() == 0) return false;   // heard direct: no first hop
  for (int i = 0; i < MAX_BLOCKHOP; i++) {
    BlockHopEntry& e = blockhop[i];
    if (e.len == 0) continue;
    uint8_t n = e.len < hs ? e.len : hs;
    if (memcmp(e.key, packet->path, n) == 0) {
      unsigned long now = millis();
      e.hits++;
      e.last_hit_ms = now;
      e.last_activity_ms = now;
      return true;
    }
  }
  return false;
}

bool MyMesh::blockHopAdd(const uint8_t* key, uint8_t len, bool is_auto) {
  if (len < 1 || len > 3) return false;
  unsigned long now = millis();
  int free_idx = -1, oldest_idx = -1;
  unsigned long oldest_age = 0;
  for (int i = 0; i < MAX_BLOCKHOP; i++) {
    BlockHopEntry& e = blockhop[i];
    if (e.len == 0) {
      if (free_idx < 0) free_idx = i;
      continue;
    }
    if (e.len == len && memcmp(e.key, key, len) == 0) {   // already listed: refresh its activity
      e.last_activity_ms = now;
      if (!is_auto) e.is_auto = false;
      return true;
    }
    unsigned long age = now - e.last_activity_ms;
    if (oldest_idx < 0 || age > oldest_age) {
      oldest_idx = i;
      oldest_age = age;
    }
  }
  int idx = (free_idx >= 0) ? free_idx : oldest_idx;   // full: replace the entry with the oldest activity
  BlockHopEntry& e = blockhop[idx];
  memset(&e, 0, sizeof(e));
  memcpy(e.key, key, len);
  e.len = len;
  e.is_auto = is_auto;
  e.last_activity_ms = now;
  if (is_auto) noteAutoBlock(key, len);   // announced on the channel (at most one notice an hour)
  return true;
}

void MyMesh::formatBlockHopList(char* reply, int page) {
  int total = 0;
  for (int i = 0; i < MAX_BLOCKHOP; i++) {
    if (blockhop[i].len) total++;
  }
  if (total == 0) {
    strcpy(reply, "blockhop: list empty");
    return;
  }
  int pages = (total + BLOCKHOP_PER_PAGE - 1) / BLOCKHOP_PER_PAGE;
  if (page < 1) page = 1;
  if (page > pages) page = pages;
  int skip = (page - 1) * BLOCKHOP_PER_PAGE;
  int shown = 0;
  unsigned long now = millis();
  size_t pos = 0;
  reply[0] = 0;
  for (int i = 0; i < MAX_BLOCKHOP; i++) {
    const BlockHopEntry& e = blockhop[i];
    if (e.len == 0) continue;
    if (skip > 0) {
      skip--;
      continue;
    }
    if (shown >= BLOCKHOP_PER_PAGE) break;
    char id[8];
    for (int j = 0; j < e.len; j++) sprintf(&id[j * 2], "%02X", e.key[j]);
    char age[12];
    if (e.hits) fmtAge(age, sizeof(age), now - e.last_hit_ms);
    else strcpy(age, "-");
    char item[48];
    snprintf(item, sizeof(item), "%s%s x%lu %s%s", pos ? " | " : "", id, (unsigned long)e.hits, age,
             e.is_auto ? " A" : "");
    size_t il = strlen(item);
    if (pos + il > CLI_REPLY_MAX - 8) break;
    memcpy(&reply[pos], item, il + 1);
    pos += il;
    shown++;
  }
  if (pages > 1) snprintf(&reply[pos], 12, " p%d/%d", page, pages);
}

void MyMesh::saveChannelKey() {   // /chan_key: 1 byte key length + 32 bytes key
  if (_fs == NULL || !chan_key_set) return;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(CHAN_KEY_FILE);
  File f = _fs->open(CHAN_KEY_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = _fs->open(CHAN_KEY_FILE, "w");
#else
  File f = _fs->open(CHAN_KEY_FILE, "w", true);
#endif
  if (f) {
    uint8_t buf[33];
    buf[0] = chan_key_len;
    memcpy(&buf[1], chan.secret, 32);
    f.write(buf, sizeof(buf));
    f.close();
  }
}

void MyMesh::loadChannelKey() {
  if (_fs == NULL || !_fs->exists(CHAN_KEY_FILE)) return;
#if defined(RP2040_PLATFORM)
  File f = _fs->open(CHAN_KEY_FILE, "r");
#else
  File f = _fs->open(CHAN_KEY_FILE);
#endif
  if (f) {
    uint8_t buf[33];
    int n = f.read(buf, sizeof(buf));
    f.close();
    if (n == 33 && (buf[0] == 16 || buf[0] == 32)) {
      memset(&chan, 0, sizeof(chan));
      memcpy(chan.secret, &buf[1], 32);
      mesh::Utils::sha256(chan.hash, sizeof(chan.hash), chan.secret, buf[0]);
      chan_key_len = buf[0];
      chan_key_set = true;
    }
  }
}

bool MyMesh::setChannelKey(const char* key) {
  uint8_t tmp[32];
  int len = -1;
  size_t kl = strlen(key);
  bool all_hex = kl > 0;
  for (size_t i = 0; i < kl; i++) {
    if (hexNibble(key[i]) < 0) all_hex = false;
  }
  if ((kl == 32 || kl == 64) && all_hex) {   // hex (a 24 or 44 character base64 key can't be confused with this)
    len = (int)(kl / 2);
    for (int i = 0; i < len; i++) {
      tmp[i] = (uint8_t)((hexNibble(key[i * 2]) << 4) | hexNibble(key[i * 2 + 1]));
    }
  } else {
    len = b64Decode(key, tmp, sizeof(tmp));
  }
  if (len != 16 && len != 32) return false;
  memset(&chan, 0, sizeof(chan));
  memcpy(chan.secret, tmp, len);
  mesh::Utils::sha256(chan.hash, sizeof(chan.hash), chan.secret, len);   // same as BaseChatMesh::addChannel()
  chan_key_len = (uint8_t)len;
  chan_key_set = true;
  return true;
}

// build and flood a channel text message, the same way BaseChatMesh::sendGroupMessage() does
bool MyMesh::postChannel(const char* text) {
  if (!chan_key_set) return false;
  uint8_t temp[5 + CHAN_MSG_MAX + 40];
  uint32_t ts = getRTCClock()->getCurrentTime();
  memcpy(temp, &ts, 4);
  temp[4] = 0;   // TXT_TYPE_PLAIN
  int prefix_len = snprintf((char*)&temp[5], 40, "%s: ", _prefs.node_name);   // <sender>: <msg>
  if (prefix_len > 39) prefix_len = 39;
  int text_len = (int)strlen(text);
  if (prefix_len + text_len > CHAN_MSG_MAX) text_len = CHAN_MSG_MAX - prefix_len;
  memcpy(&temp[5 + prefix_len], text, text_len);
  temp[5 + prefix_len + text_len] = 0;
  mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, chan, temp, 5 + prefix_len + text_len);
  if (pkt == NULL) return false;
  sendFloodScoped(default_scope, pkt, 0, _prefs.path_hash_mode + 1);
  return true;
}

void MyMesh::chanEnqueue(const char* text, uint32_t gap_after_ms, bool is_report) {
  if (!chan_key_set) return;            // no key: skipped silently, never retried
  if (chan_q_n >= CHAN_Q_MAX) return;   // queue full: drop
  ChanQueued& q = chan_q[chan_q_n++];
  strncpy(q.text, text, CHAN_MSG_MAX);
  q.text[CHAN_MSG_MAX] = 0;
  q.gap_after_ms = gap_after_ms;
  q.is_report = is_report;
}

void MyMesh::chanDropReports() {   // an older daily report that never got out is replaced by the new one
  int w = 0;
  for (int i = 0; i < chan_q_n; i++) {
    if (!chan_q[i].is_report) {
      if (w != i) chan_q[w] = chan_q[i];
      w++;
    }
  }
  chan_q_n = (uint8_t)w;
}

void MyMesh::chanService() {   // post queued channel messages, one at a time, only while the radio is awake
  if (chan_q_n == 0) return;
  if (!chan_key_set) {
    chan_q_n = 0;
    return;
  }
  if (chan_gap_on && !millisHasNowPassed(chan_next_send_at)) return;
  ChanQueued q = chan_q[0];
  for (int i = 1; i < chan_q_n; i++) chan_q[i - 1] = chan_q[i];
  chan_q_n--;
  postChannel(q.text);
  chan_gap_on = true;
  chan_next_send_at = futureMillis(q.gap_after_ms);
}

void MyMesh::dailyReport() {   // every 24h from boot: battlog line, then templog line
  if (!report_armed) {
    report_armed = true;
    report_due_at = futureMillis(REPORT_INTERVAL_MS);
    return;
  }
  if (!millisHasNowPassed(report_due_at)) return;
  report_due_at = futureMillis(REPORT_INTERVAL_MS);   // the next report is always 24h after this one
  chanDropReports();
  queueReport();
}

void MyMesh::queueReport() {   // battlog line, then templog line (skipped without a key or without data)
  if (!chan_key_set || batt_log_count == 0) return;
  char buf[CHAN_MSG_MAX + 1];
  int prefix = (int)strlen(_prefs.node_name) + 2;
  if (prefix > 39) prefix = 39;
  formatBattLog(buf);
  trimLogToFit(buf, 12, prefix);
  chanEnqueue(buf, CHAN_PAIR_GAP_MS, true);
  formatTempLog(buf);
  trimLogToFit(buf, 12, prefix);
  chanEnqueue(buf, CHAN_GAP_MS, true);
}

void MyMesh::formatStatus(char* reply) {
  unsigned mv = board.getBattMilliVolts();
  float t = board.getMCUTemperature();
  int blocked = 0;
  for (int i = 0; i < MAX_BLOCKHOP; i++) {
    if (blockhop[i].len) blocked++;
  }
  unsigned long up = (unsigned long)(uptime_millis / 1000ULL);
  int pos = snprintf(reply, 24, "Bat %u.%02uV", mv / 1000, (mv % 1000) / 10);
  if (!isnan(t)) {
    int t10 = (int)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f));
    int a = t10 < 0 ? -t10 : t10;
    pos += snprintf(&reply[pos], 16, ", %s%d.%dC", t10 < 0 ? "-" : "", a / 10, a % 10);
  }
  snprintf(&reply[pos], CLI_REPLY_MAX - pos, ", cycle %s, auto %s, blocked %d, key %s, up %lud%luh%lum",
           cycle_active ? "on" : "off", sleep_auto_enabled ? "on" : "off", blocked,
           chan_key_set ? "set" : "not set", up / 86400UL, (up % 86400UL) / 3600UL, (up % 3600UL) / 60UL);
}

void MyMesh::noteAutoBlock(const uint8_t* key, uint8_t len) {
  for (int i = 0; i < blk_pending_n; i++) {
    if (blk_pending_len[i] == len && memcmp(blk_pending[i], key, len) == 0) return;
  }
  if (blk_pending_n >= MAX_BLOCKHOP) return;
  memcpy(blk_pending[blk_pending_n], key, len);
  blk_pending_len[blk_pending_n] = len;
  blk_pending_n++;
}

void MyMesh::blockNoticeService() {   // at most one notice an hour, listing every new ID since the last one
  if (blk_pending_n == 0) return;
  if (blk_msg_armed && !millisHasNowPassed(blk_next_msg)) return;
  char msg[CHAN_MSG_MAX + 1];
  int pos = snprintf(msg, sizeof(msg), "Auto-blocked repeater");
  for (int i = 0; i < blk_pending_n; i++) {
    if (pos + 2 + 6 + 1 > 100) break;   // keep the notice short
    char id[8];
    for (int j = 0; j < blk_pending_len[i]; j++) sprintf(&id[j * 2], "%02X", blk_pending[i][j]);
    pos += snprintf(&msg[pos], 12, "%s%s", i ? ", " : " ", id);
  }
  blk_pending_n = 0;
  blk_msg_armed = true;
  blk_next_msg = futureMillis(BLOCK_NOTICE_GAP_MS);
  chanEnqueue(msg, CHAN_GAP_MS);
}

int MyMesh::searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) {
  if (chan_key_set && max_matches > 0 && chan.hash[0] == hash[0]) {
    channels[0] = chan;
    return 1;
  }
  return 0;   // no key set: channel messages are not read
}

void MyMesh::onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) {
  if (type != PAYLOAD_TYPE_GRP_TXT || len < 6) return;
  if ((data[4] >> 2) != 0) return;   // plain text only
  uint32_t ts;
  memcpy(&ts, data, 4);
  data[len] = 0;   // text is padded with zeroes (same as BaseChatMesh::onGroupDataRecv)
  handleChannelCommand((const char*)&data[5], ts);
}

// channel commands look like  <battlog all>  <templog Tuna>  <sleep until 21:00 all>
void MyMesh::handleChannelCommand(const char* text, uint32_t ts) {
  const char* body = strstr(text, ": ");   // "<sender>: <message>"
  if (body == NULL) return;
  body += 2;
  while (*body == ' ') body++;
  int n = (int)strlen(body);
  while (n > 0 && body[n - 1] == ' ') n--;
  if (n < 5 || body[0] != '<' || body[n - 1] != '>') return;
  int inner = n - 2;
  if (inner > CHAN_MSG_MAX) return;
  char buf[CHAN_MSG_MAX + 1];
  memcpy(buf, body + 1, inner);
  buf[inner] = 0;

  int cmd_len = -1;   // addressed to everyone, or to this repeater by name?
  if (!endsWithTarget(buf, inner, "all", &cmd_len) && !endsWithTarget(buf, inner, _prefs.node_name, &cmd_len)) return;
  while (cmd_len > 0 && buf[cmd_len - 1] == ' ') cmd_len--;
  buf[cmd_len] = 0;

  bool timed_sleep = (strncmp(buf, "sleep until ", 12) == 0) || (strncmp(buf, "sleep ", 6) == 0 && buf[6] >= '0' && buf[6] <= '9');
  bool secret_get = false;   // never hand out the private key or passwords over the channel
  if (strncmp(buf, "get ", 4) == 0) {
    for (const char* p = buf + 4; *p; p++) {
      if (strncmp(p, "prv", 3) == 0 || strncmp(p, "password", 8) == 0 || strncmp(p, "secret", 6) == 0) secret_get = true;
    }
  }
  bool allowed = timed_sleep || strcmp(buf, "battlog") == 0 || strcmp(buf, "templog") == 0
                 || strcmp(buf, "status") == 0 || strcmp(buf, "report") == 0 || strcmp(buf, "advert") == 0
                 || (strncmp(buf, "get ", 4) == 0 && !secret_get)   // every 'get' command except the secrets
                 || strncmp(buf, "blockhop ", 9) == 0   // blockhop add / remove / list / clear
                 || strcmp(buf, "sleep cycle on") == 0 || strcmp(buf, "sleep cycle off") == 0
                 || strcmp(buf, "sleep auto on") == 0 || strcmp(buf, "sleep auto off") == 0;
  if (!allowed) return;   // only these commands can be given over the channel

  if (ts <= last_cmd_ts) return;   // replayed or duplicate command
  last_cmd_ts = ts;

  char reply[192];
  reply[0] = 0;
  from_channel = true;
  if (strcmp(buf, "advert") == 0) {   // flood advert, delayed at random so repeaters don't all send at once
    sendSelfAdvertisement(getRNG()->nextInt(2000, 60001), true);
    strcpy(reply, "OK - advert sent");
  } else {
    handleCommand(0, buf, reply);   // (0 = no clock sync from channel messages)
  }
  from_channel = false;
  if (reply[0] != 0) {
    if ((reply[0] == 'V' || reply[0] == 'C') && reply[1] == '/') {   // battlog / templog line: make it fit
      int prefix = (int)strlen(_prefs.node_name) + 2;
      if (prefix > 39) prefix = 39;
      trimLogToFit(reply, 12, prefix);
    }
    chanEnqueue(reply, CHAN_GAP_MS);
  }
  // every repeater answers a command for 'all': spread the replies out by a random 5..60 seconds
  chan_gap_on = true;
  chan_next_send_at = futureMillis(getRNG()->nextInt(5000, 60001));
}

bool MyMesh::handleSleepCommands(uint32_t sender_timestamp, char* command, char* reply) {
  size_t cl = strlen(command);
  while (cl > 0 && command[cl - 1] == ' ') command[--cl] = 0;   // trim trailing spaces

  if (strcmp(command, "sleep cycle on") == 0) {
    if (cycle_active) {
      strcpy(reply, "sleep cycle already running");
      return true;
    }
    unsigned mv = board.getBattMilliVolts();
    if (mv >= CYCLE_STOP_MV) {
      snprintf(reply, 60, "Err - battery %u.%02uV is at/above %u.%02uV", mv / 1000, (mv % 1000) / 10,
               CYCLE_STOP_MV / 1000, (CYCLE_STOP_MV % 1000) / 10);
      return true;
    }
    cycle_active = true;
    startSleep(CYCLE_SLEEP_MS);
    strcpy(reply, "sleep cycle started");
    return true;
  }

  if (strcmp(command, "sleep cycle off") == 0) {
    if (cycle_active) {
      stopSleepCycle();
      strcpy(reply, "sleep cycle stopped");
    } else {
      strcpy(reply, "sleep cycle not running");
    }
    return true;
  }

  if (strcmp(command, "sleep auto on") == 0 || strcmp(command, "sleep auto off") == 0) {
    sleep_auto_enabled = (command[11] == 'o' && command[12] == 'n');
    strcpy(reply, sleep_auto_enabled ? "OK - sleep auto on" : "OK - sleep auto off");
    return true;
  }

  if (memcmp(command, "sleep until ", 12) == 0) {   // 'sleep until HH:MM' (Brisbane time)
    const char* p = &command[12];
    const char* colon = strchr(p, ':');
    if (colon == NULL || p[0] < '0' || p[0] > '9' || colon[1] < '0' || colon[1] > '9') {
      strcpy(reply, "Err - use: sleep until HH:MM (Brisbane time)");
      return true;
    }
    int hh = atoi(p);
    int mm = atoi(colon + 1);
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
      strcpy(reply, "Err - use: sleep until HH:MM (Brisbane time)");
      return true;
    }
    uint32_t now = getRTCClock()->getCurrentTime();
    bool synced = false;
    if (sender_timestamp > now) {   // same rule as 'clock sync': only ever move the clock forward
      getRTCClock()->setCurrentTime(sender_timestamp + 1);
      now = getRTCClock()->getCurrentTime();
      synced = true;
    }
    if (now < CLOCK_VALID_AFTER) {
      strcpy(reply, "Err - clock not set, use: clock sync");
      return true;
    }
    uint32_t sod = (now + BRISBANE_UTC_OFFSET_SECS) % 86400UL;   // seconds since local midnight
    uint32_t target = (uint32_t)hh * 3600UL + (uint32_t)mm * 60UL;
    uint32_t diff = (target + 86400UL - sod) % 86400UL;
    if (diff == 0) diff = 86400UL;
    if (diff < 60) {
      strcpy(reply, "Err - too soon (under 1 min)");
      return true;
    }
    cycle_active = false;   // a manual sleep overrides a running cycle
    startSleep(diff * 1000UL, true);   // wakes exactly at the requested time, however long the radio stays on first
    if (!from_channel) {   // (a command given on the channel gets its normal reply posted instead)
      char note[48];
      snprintf(note, sizeof(note), "Sleeping until %02d:%02d", hh, mm);
      chanEnqueue(note, CHAN_GAP_MS);
    }
    snprintf(reply, 100, "OK - sleeping until %02d:%02d (%lu mins)%s", hh, mm, (unsigned long)((diff + 30) / 60),
             synced ? ", clock synced" : "");
    return true;
  }

  if (memcmp(command, "sleep ", 6) == 0) {   // 'sleep <minutes>'
    int mins = atoi(&command[6]);
    if (mins < 1 || mins > 1440) {
      strcpy(reply, "Err - 1..1440 mins");
    } else {
      cycle_active = false;   // a manual sleep overrides a running cycle
      startSleep((uint32_t)mins * 60000UL);
      sprintf(reply, "OK - sleeping for %d mins", mins);
      if (!from_channel) {
        char note[48];
        snprintf(note, sizeof(note), "Sleeping for %d mins", mins);
        chanEnqueue(note, CHAN_GAP_MS);
      }
    }
    return true;
  }

  if (strcmp(command, "battlog") == 0) {
    formatBattLog(reply);
    return true;
  }

  if (strcmp(command, "status") == 0) {
    formatStatus(reply);
    return true;
  }

  if (strcmp(command, "report") == 0) {   // post the daily report now
    if (!chan_key_set) {
      strcpy(reply, "Err - channel key not set");
    } else if (batt_log_count == 0) {
      strcpy(reply, "Err - no log data yet");
    } else {
      chanDropReports();
      queueReport();
      if (from_channel) reply[0] = 0;   // the two report messages are the answer
      else strcpy(reply, "OK - report queued");
    }
    return true;
  }

  if (memcmp(command, "channel ", 8) == 0 || strcmp(command, "channel") == 0) {
    const char* sub = command + (command[7] == ' ' ? 8 : 7);
    while (*sub == ' ') sub++;
    if (memcmp(sub, "set ", 4) == 0) {
      const char* key = sub + 4;
      while (*key == ' ') key++;
      if (setChannelKey(key)) {
        saveChannelKey();
        strcpy(reply, "OK - channel key set and saved");
      } else {
        strcpy(reply, "Err - key must be base64 or hex, 16 or 32 bytes");
      }
    } else if (strcmp(sub, "get") == 0) {
      if (!chan_key_set) {
        strcpy(reply, "channel key not set");
      } else {
        char b64[48];
        b64Encode(chan.secret, chan_key_len, b64);
        snprintf(reply, 100, "key: %s", b64);
      }
    } else if (strcmp(sub, "clear") == 0) {
      chan_key_set = false;
      chan_key_len = 0;
      memset(&chan, 0, sizeof(chan));
      chan_q_n = 0;
      if (_fs) _fs->remove(CHAN_KEY_FILE);
      strcpy(reply, "OK - channel key cleared");
    } else {
      strcpy(reply, "Err - channel set <key>|get|clear");
    }
    return true;
  }

  if (strcmp(command, "say") == 0) {
    strcpy(reply, "Err - say <text>");
    return true;
  }
  if (memcmp(command, "say ", 4) == 0) {
    const char* text = command + 4;
    while (*text == ' ') text++;
    if (*text == 0) {
      strcpy(reply, "Err - say <text>");
    } else if (!chan_key_set) {
      strcpy(reply, "Err - channel key not set");
    } else {
      int prefix = (int)strlen(_prefs.node_name) + 2;
      if (prefix > 39) prefix = 39;
      int maxlen = CHAN_MSG_MAX - prefix;
      if ((int)strlen(text) > maxlen) {
        snprintf(reply, 60, "Err - too long (max %d chars)", maxlen);
      } else {
        strcpy(reply, postChannel(text) ? "OK - posted" : "Err - unable to send");
      }
    }
    return true;
  }

  if (strcmp(command, "templog") == 0) {
    formatTempLog(reply);
    return true;
  }

  if (memcmp(command, "blockhop", 8) == 0 && (command[8] == 0 || command[8] == ' ')) {
    const char* sub = command + 8;
    while (*sub == ' ') sub++;
    if (strcmp(sub, "list") == 0 || memcmp(sub, "list ", 5) == 0) {
      formatBlockHopList(reply, sub[4] == ' ' ? atoi(&sub[5]) : 1);
    } else if (strcmp(sub, "clear") == 0) {
      memset(blockhop, 0, sizeof(blockhop));
      strcpy(reply, "OK - blockhop cleared");
    } else if (memcmp(sub, "add ", 4) == 0 || memcmp(sub, "remove ", 7) == 0) {
      bool is_add = (sub[0] == 'a');
      const char* hex = sub + (is_add ? 4 : 7);
      while (*hex == ' ') hex++;
      uint8_t key[3];
      int len = parseHexId(hex, key);
      if (len == 0) {
        strcpy(reply, "Err - id must be 2, 4 or 6 hex chars");
      } else if (is_add) {
        blockHopAdd(key, (uint8_t)len, false);
        strcpy(reply, "OK - blocked");
      } else {
        int removed = 0;
        for (int i = 0; i < MAX_BLOCKHOP; i++) {
          if (blockhop[i].len == len && memcmp(blockhop[i].key, key, len) == 0) {
            memset(&blockhop[i], 0, sizeof(blockhop[i]));
            removed++;
          }
        }
        strcpy(reply, removed ? "OK - removed" : "Err - not in list");
      }
    } else {
      strcpy(reply, "Err - blockhop add|remove|list|clear");
    }
    return true;
  }

  return false;
}

bool MyMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return InternalFS.format();
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  return SPIFFS.format();
#else
#error "need to implement file system erase"
  return false;
#endif
}

void MyMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
  mesh::Packet *pkt = createSelfAdvert();
  if (pkt) {
    if (flood) {
      sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
    } else {
      sendZeroHop(pkt, delay_millis);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create advertisement packet!");
  }
}

void MyMesh::updateAdvertTimer() {
  if (_prefs.advert_interval > 0) { // schedule local advert timer
    next_local_advert = futureMillis(((uint32_t)_prefs.advert_interval) * 2 * 60 * 1000);
  } else {
    next_local_advert = 0; // stop the timer
  }
}

void MyMesh::updateFloodAdvertTimer() {
  if (_prefs.flood_advert_interval > 0) { // schedule flood advert timer
    next_flood_advert = futureMillis(((uint32_t)_prefs.flood_advert_interval) * 60 * 60 * 1000);
  } else {
    next_flood_advert = 0; // stop the timer
  }
}

void MyMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
  File f = _fs->open(PACKET_LOG_FILE, "r");
#else
  File f = _fs->open(PACKET_LOG_FILE);
#endif
  if (f) {
    while (f.available()) {
      int c = f.read();
      if (c < 0) break;
      Serial.print((char)c);
    }
    f.close();
  }
}

void MyMesh::setTxPower(int8_t power_dbm) {
  radio_driver.setTxPower(power_dbm);
}

bool MyMesh::setRxBoostedGain(bool enable) {
  return radio_driver.setRxBoostedGainMode(enable);
}

#if defined(USE_LR2021)
bool MyMesh::configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) {
  return radio_driver.configSideDetectors(sideDetSFs, num, bw);
}
#endif

void MyMesh::formatNeighborsReply(char *reply) {
  char *dp = reply;

#if MAX_NEIGHBOURS
  // create copy of neighbours list, skipping empty entries so we can sort it separately from main list
  int16_t neighbours_count = 0;
  NeighbourInfo* sorted_neighbours[MAX_NEIGHBOURS];
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    auto neighbour = &neighbours[i];
    if (neighbour->heard_timestamp > 0) {
      sorted_neighbours[neighbours_count] = neighbour;
      neighbours_count++;
    }
  }

  // sort neighbours newest to oldest
  std::sort(sorted_neighbours, sorted_neighbours + neighbours_count, [](const NeighbourInfo* a, const NeighbourInfo* b) {
    return a->heard_timestamp > b->heard_timestamp; // desc
  });

  for (int i = 0; i < neighbours_count && dp - reply < 134; i++) {
    NeighbourInfo *neighbour = sorted_neighbours[i];

    // add new line if not first item
    if (i > 0) *dp++ = '\n';

    char hex[10];
    // get 4 bytes of neighbour id as hex
    mesh::Utils::toHex(hex, neighbour->id.pub_key, 4);

    // add next neighbour
    uint32_t secs_ago = getRTCClock()->getCurrentTime() - neighbour->heard_timestamp;
    sprintf(dp, "%s:%d:%d", hex, secs_ago, neighbour->snr);
    while (*dp)
      dp++; // find end of string
  }
#endif
  if (dp == reply) { // no neighbours, need empty response
    strcpy(dp, "-none-");
    dp += 6;
  }
  *dp = 0; // null terminator
}

void MyMesh::removeNeighbor(const uint8_t *pubkey, int key_len) {
#if MAX_NEIGHBOURS
  for (int i = 0; i < MAX_NEIGHBOURS; i++) {
    NeighbourInfo *neighbour = &neighbours[i];
    if (memcmp(neighbour->id.pub_key, pubkey, key_len) == 0) {
      neighbours[i] = NeighbourInfo(); // clear neighbour entry
    }
  }
#endif
}

void MyMesh::startRegionsLoad() {
  temp_map.resetFrom(region_map);   // rebuild regions in a temp instance
  memset(load_stack, 0, sizeof(load_stack));
  load_stack[0] = &temp_map.getWildcard();
  region_load_active = true;
}

bool MyMesh::saveRegions() {
  return region_map.save(_fs);
}

void MyMesh::onDefaultRegionChanged(const RegionEntry* r) {
  if (r) {
    region_map.getTransportKeysFor(*r, &default_scope, 1);
  } else {
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }
}

void MyMesh::formatStatsReply(char *reply) {
  StatsFormatHelper::formatCoreStats(reply, board, *_ms, _err_flags, _mgr);
}

void MyMesh::formatRadioStatsReply(char *reply) {
  StatsFormatHelper::formatRadioStats(reply, _radio, radio_driver, getTotalAirTime(), getReceiveAirTime());
}

void MyMesh::formatPacketStatsReply(char *reply) {
  StatsFormatHelper::formatPacketStats(reply, radio_driver, getNumSentFlood(), getNumSentDirect(), 
                                       getNumRecvFlood(), getNumRecvDirect());
}

void MyMesh::saveIdentity(const mesh::LocalIdentity &new_id) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  IdentityStore store(*_fs, "");
#elif defined(ESP32)
  IdentityStore store(*_fs, "/identity");
#elif defined(RP2040_PLATFORM)
  IdentityStore store(*_fs, "/identity");
#else
#error "need to define saveIdentity()"
#endif
  store.save("_main", new_id);
}

void MyMesh::clearStats() {
  radio_driver.resetStats();
  resetStats();
  ((SimpleMeshTables *)getTables())->resetStats();
}

void MyMesh::handleCommand(uint32_t sender_timestamp, char *command, char *reply) {
  if (region_load_active) {
    if (StrHelper::isBlank(command)) {  // empty/blank line, signal to terminate 'load' operation
      region_map = temp_map;  // copy over the temp instance as new current map
      region_load_active = false;

      sprintf(reply, "OK - loaded %d regions", region_map.getCount());
    } else {
      char *np = command;
      while (*np == ' ') np++;   // skip indent
      int indent = np - command;

      char *ep = np;
      while (RegionMap::is_name_char(*ep)) ep++;
      if (*ep) { *ep++ = 0; }  // set null terminator for end of name

      while (*ep && *ep != 'F') ep++;  // look for (optional) flags

      if (indent > 0 && indent < 8 && strlen(np) > 0) {
        auto parent = load_stack[indent - 1];
        if (parent) {
          auto old = region_map.findByName(np);
          auto nw = temp_map.putRegion(np, parent->id, old ? old->id : 0);  // carry-over the current ID (if name already exists)
          if (nw) {
            nw->flags = old ? old->flags : (*ep == 'F' ? 0 : REGION_DENY_FLOOD);   // carry-over flags from curr

            load_stack[indent] = nw;  // keep pointers to parent regions, to resolve parent_id's
          }
        }
      }
      reply[0] = 0;
    }
    return;
  }

  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    command += 3;
  }

  if (handleSleepCommands(sender_timestamp, command, reply)) return;   // sleep*, battlog, blockhop

  // handle ACL related commands
  if (memcmp(command, "setperm ", 8) == 0) {   // format:  setperm {pubkey-hex} {permissions-int8}
    char* hex = &command[8];
    char* sp = strchr(hex, ' ');   // look for separator char
    if (sp == NULL) {
      strcpy(reply, "Err - bad params");
    } else {
      *sp++ = 0;   // replace space with null terminator

      uint8_t pubkey[PUB_KEY_SIZE];
      int hex_len = min(sp - hex, PUB_KEY_SIZE*2);
      if (mesh::Utils::fromHex(pubkey, hex_len / 2, hex)) {
        uint8_t perms = atoi(sp);
        if (acl.applyPermissions(self_id, pubkey, hex_len / 2, perms)) {
          dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);   // trigger acl.save()
          strcpy(reply, "OK");
        } else {
          strcpy(reply, "Err - invalid params");
        }
      } else {
        strcpy(reply, "Err - bad pubkey");
      }
    }
  } else if (sender_timestamp == 0 && strcmp(command, "get acl") == 0) {
    Serial.println("ACL:");
    for (int i = 0; i < acl.getNumClients(); i++) {
      auto c = acl.getClientByIdx(i);
      if (c->permissions == 0) continue;  // skip deleted (or guest) entries

      Serial.printf("%02X ", c->permissions);
      mesh::Utils::printHex(Serial, c->id.pub_key, PUB_KEY_SIZE);
      Serial.printf("\n");
    }
    reply[0] = 0;
  } else if (memcmp(command, "discover.neighbors", 18) == 0) {
    const char* sub = command + 18;
    while (*sub == ' ') sub++;
    if (*sub != 0) {
      strcpy(reply, "Err - discover.neighbors has no options");
    } else {
      sendNodeDiscoverReq();
      strcpy(reply, "OK - Discover sent");
    }
  } else{
    _cli.handleCommand(sender_timestamp, command, reply);  // common CLI commands
  }
}

void MyMesh::loop() {
  // battery monitor: every 5 min (also runs while the radio sleeps)
  if (!batt_check_armed) {
    batt_check_armed = true;
    next_batt_check = futureMillis(BATT_CHECK_MS);
  } else if (millisHasNowPassed(next_batt_check)) {
    next_batt_check = futureMillis(BATT_CHECK_MS);
    batteryMonitor();
  }

  dailyReport();           // 24h report timer (counts while asleep too)
  blockNoticeService();    // announce newly auto-blocked repeaters
  if (!sleeping) chanService();   // post queued channel messages while the radio is awake

  // A packet leaves the send queue when its transmission STARTS, and the transmission takes seconds.
  // So after the last queued send, stay awake for a quiet period before the radio may go to sleep.
  if (!sleeping && hasPendingWork()) {
    busy_armed = true;
    busy_until = futureMillis(TX_QUIET_MS);
  }
  bool radio_quiet = !busy_armed || millisHasNowPassed(busy_until);

  // sleep (CLI: "sleep <mins>", "sleep until", or the sleep cycle): radio fully off, no RX/TX
  if (sleep_pending && millisHasNowPassed(sleep_start_at) && !hasPendingWork() && radio_quiet
      && (chan_q_n == 0 || millisHasNowPassed(sleep_deadline))) {
    sleep_pending = false;
    if (!(sleep_target_abs && millisHasNowPassed(sleep_target_at))) {   // (target already passed: nothing to sleep)
      radioSleep(sleep_duration_ms);
      if (sleep_target_abs) sleep_wake_at = sleep_target_at;
    }
  }
  if (sleeping) {
    if (!millisHasNowPassed(sleep_wake_at)) {
      delay(500);  // idle; serial CLI (e.g. "reboot") is still serviced by main loop()
      return;
    }
    radioWake();
    if (cycle_active) {   // start of an awake window
      cycle_awake_until = futureMillis(CYCLE_AWAKE_MS);
      unsigned mv = board.getBattMilliVolts();
      if (mv >= CYCLE_STOP_MV) {   // charged: leave the sleep cycle and announce we are back
        stopSleepCycle();
        sendSelfAdvertisement(2000, true);
        char msg[CHAN_MSG_MAX + 1];
        snprintf(msg, sizeof(msg), "Sleep cycle ended, battery %u.%02uV, repeating resumed",
                 (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
        chanEnqueue(msg, CHAN_GAP_MS);
        chan_gap_on = true;
        chan_next_send_at = futureMillis(CHAN_PAIR_GAP_MS);   // message follows the advert by ~10s
      } else {   // still cycling: tell the channel that the repeater is awake now
        char msg[CHAN_MSG_MAX + 1];
        snprintf(msg, sizeof(msg), "Awake for %lu mins", (unsigned long)(CYCLE_AWAKE_MS / 60000UL));
        chanEnqueue(msg, CHAN_GAP_MS);
        chan_gap_on = true;
        chan_next_send_at = futureMillis(getRNG()->nextInt(2000, 20001));   // spread out nodes that wake together
      }
    }
  }
  // end of an awake window: back to sleep (an admin login stops the cycle before this)
  if (cycle_active && !sleeping && !sleep_pending && millisHasNowPassed(cycle_awake_until) && !hasPendingWork() && radio_quiet) {
    radioSleep(CYCLE_SLEEP_MS);
    return;
  }

#ifdef WITH_BRIDGE
  bridge.loop();
#endif

  mesh::Mesh::loop();

  if (next_flood_advert && millisHasNowPassed(next_flood_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    uint32_t delay_millis = 0;
    if (pkt) sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);

    updateFloodAdvertTimer(); // schedule next flood advert
    updateAdvertTimer();      // also schedule local advert (so they don't overlap)
  } else if (next_local_advert && millisHasNowPassed(next_local_advert)) {
    mesh::Packet *pkt = createSelfAdvert();
    if (pkt) sendZeroHop(pkt);

    updateAdvertTimer(); // schedule next local advert
  }

  if (set_radio_at && millisHasNowPassed(set_radio_at)) { // apply pending (temporary) radio params
    set_radio_at = 0;                                     // clear timer
    radio_driver.setParams(pending_freq, pending_bw, pending_sf, pending_cr);
    MESH_DEBUG_PRINTLN("Temp radio params");
  }

  if (revert_radio_at && millisHasNowPassed(revert_radio_at)) { // revert radio params to orig
    revert_radio_at = 0;                                        // clear timer
    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("Radio params restored");
  }

  // is pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    acl.save(_fs);
    dirty_contacts_expiry = 0;
  }

  // update uptime
  uint32_t now = millis();
  uptime_millis += now - last_millis;
  last_millis = now;
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
#if defined(WITH_BRIDGE)
  if (bridge.isRunning()) return true;  // bridge needs WiFi radio, can't sleep
#endif
  return _mgr->getOutboundTotal() > 0;
}
