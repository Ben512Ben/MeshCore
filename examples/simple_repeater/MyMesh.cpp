#include "MyMesh.h"
#include "HikeBot.h"    // one bot for every "!" message: trails, hiking help, signal test and chat (uses TrailBot.h, ChatBot.h)
#include <algorithm>
#ifdef HELTEC_LORA_V4
#include <driver/gpio.h>   // gpio_pulldown_en() for DIO1 while the radio sleeps
#endif

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
#define SLEEP_MSG_WAIT_MS 150000                  // sleep waits at most this long for queued channel messages
#define TX_QUIET_MS       6000                    // radio never goes to sleep until this long after the last queued send
#define STAGGER_BASE_S    112                     // reply delay of a sleep command sent to 'all': 112 s at 0 hops ...
#define STAGGER_STEP_S    10                      //   ... 10 s less per hop ...
#define STAGGER_SPREAD_S  8                       //   ... plus a random 0..8 s ...
#define BOT_FIRST_MIN_MS  3000                    // trail bot: first reply 3 to 15 s after the request ...
#define BOT_FIRST_MAX_MS  15000
#define BOT_GAP_MS        10000                   //   ... then 10 s between the messages of one answer
#define BOT_USER_COOLDOWN_MS 10000                //   ... and one person gets no new answer for 10 s after their last message
#define STAGGER_FLOOR_S   5                       //   ... and never less than 5 s (so the furthest repeaters sleep first)

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
  if (block_name_hit) return false;                // sender is on the blockname list (Public / filter channels)
  if (chanAllowBlocks(packet)) return false;       // channel is not on the chanallow whitelist
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
  block_name_hit = false;   // the mark is only valid while this one packet is processed
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
  memset(blockname, 0, sizeof(blockname));
  block_name_hit = false;
  memset(filter_keys, 0, sizeof(filter_keys));
  chanallow_on = chanallow_extra_set = false;
  chanallow_extra_hash = 0;
  chanallow_dropped = 0;
  cmd_hops = -1;
  sleep_cmd_flag = false;
  {   // built-in Public channel key (publicly known): used read-only, to check sender names
    static const uint8_t PUBLIC_KEY[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                                           0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
    memset(&public_chan, 0, sizeof(public_chan));
    memcpy(public_chan.secret, PUBLIC_KEY, sizeof(PUBLIC_KEY));
    mesh::Utils::sha256(public_chan.hash, sizeof(public_chan.hash), public_chan.secret, sizeof(PUBLIC_KEY));
  }
  hot_count = 0;
  hot_msg_sent = crit_msg_sent = false;
  blk_pending_n = 0;
  blk_msg_armed = false;
  blk_next_msg = 0;
  sleep_deadline = 0;
  chan_key_set = false;
  chan_key_len = 0;
  memset(&chan, 0, sizeof(chan));
  bot_key_set = bot_on = false;   // trail bot: off after every boot
  bot_key_len = 0;
  memset(&bot_chan, 0, sizeof(bot_chan));
  test_on = false;   // test channels: off after every boot too
  memset(test_slot_set, 0, sizeof(test_slot_set));
  memset(test_key_len, 0, sizeof(test_key_len));
  memset(test_chans, 0, sizeof(test_chans));
  test_replies = 0;
  bot_q_n = 0;
  bot_ans_n = 0;
  memset(bot_ans_len, 0, sizeof(bot_ans_len));
  memset(bot_ans_ch, 0, sizeof(bot_ans_ch));
  memset(bot_ans_who, 0, sizeof(bot_ans_who));
  memset(bot_cd, 0, sizeof(bot_cd));
  bot_next_send_at = 0;
  bot_replies = 0;
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
  boot_notice_pending = true;   // announce the (re)start on the channel
  boot_notice_armed = false;
  boot_notice_at = 0;
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
  loadBotKey();       // trail bot key saved by 'botchan set' (the bot itself stays off until 'botchan on')
  loadTestKeys();     // test channel keys saved by 'testchan set' (they stay off until 'testchan on')

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
  sleep_cmd_flag = true;   // (handleChannelCommand checks this to pick the reply delay)
}

bool MyMesh::sleepFor(uint32_t duration_ms) {   // (CommonCLI hook; 'sleep <mins>' is handled in handleSleepCommands)
  cycle_active = false;   // a manual sleep overrides a running cycle
  startSleep(duration_ms);
  return true;
}

void MyMesh::radioSleep(uint32_t duration_ms) {
  sleeping = true;
  botQueueClear();   // trail bot replies still waiting are dropped, not held for the next wake
  sleep_wake_at = futureMillis(duration_ms);
  radio_driver.powerOff();
#ifdef HELTEC_T096
  t096FrontEndOff();
#endif
#ifdef HELTEC_LORA_V4
  board.loRaFEMControl.setSleepModeEnable();            // front-end chip into its low-power mode (supply stays on)
  gpio_pullup_dis((gpio_num_t)P_LORA_DIO_1);            // hold DIO1 low while the radio sleeps, so a floating pin
  gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);           //   can't keep the MCU from light-sleeping
#endif
}

void MyMesh::radioWake() {
  sleeping = false;
  uint32_t saved_time = getRTCClock()->getCurrentTime();
#ifdef HELTEC_T096
  t096FrontEndOn();                   // front-end chip must be powered before the radio restarts
#endif
#ifdef HELTEC_LORA_V4
  gpio_pulldown_dis((gpio_num_t)P_LORA_DIO_1);          // the radio drives DIO1 itself again
  board.loRaFEMControl.setRxModeEnable();               // front-end chip back to receive mode before the radio restarts
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
#ifdef NRF52_POWER_MANAGEMENT   // only these boards really shut down (boot lock)
      snprintf(msg, sizeof(msg), "Battery critical %u.%02uV, shutting down until recharged",
               (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
#else
      snprintf(msg, sizeof(msg), "Battery critical %u.%02uV", (unsigned)(mv / 1000), (unsigned)((mv % 1000) / 10));
#endif
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

// Battery percent from voltage: straight line from 3.00 V (0%) to 4.20 V (100%), whole percent
// rounded down. This is the same conversion the MeshCore app uses, so the numbers match the app.
static int battPercent(unsigned mv) {
  if (mv <= 3000) return 0;
  if (mv >= 4200) return 100;
  return (int)(((uint32_t)(mv - 3000) * 100) / 1200);
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

void MyMesh::formatBattPct(char* reply) {
  if (batt_log_count == 0) {
    strcpy(reply, "battpct: no data yet (first reading 5 min after boot)");
    return;
  }
  int pos = snprintf(reply, 20, "%%/h old>new:");
  for (int i = 0; i < batt_log_count; i++) {
    int idx = (batt_log_head + 24 - batt_log_count + i) % 24;
    if (pos > CLI_REPLY_MAX - 6) break;   // never overrun the reply buffer
    pos += snprintf(&reply[pos], 6, " %d", battPercent(batt_log[idx]));
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

// ---------------------- 'blockname', 'filterchan' and 'chanallow' ----------------------

static bool eqNoCase(const char* a, const char* b) {
  while (*a && *b) {
    if (lowerc((unsigned char)*a) != lowerc((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

// does a listed name match a sender name? exact (ignoring case), or a prefix if the listed name ends with '*'
static bool nameMatches(const char* entry, const char* sender, int slen) {
  int el = (int)strlen(entry);
  if (el > 0 && entry[el - 1] == '*') {
    int pl = el - 1;
    if (slen < pl) return false;
    for (int i = 0; i < pl; i++) {
      if (lowerc((unsigned char)entry[i]) != lowerc((unsigned char)sender[i])) return false;
    }
    return true;
  }
  if (el != slen) return false;
  for (int i = 0; i < el; i++) {
    if (lowerc((unsigned char)entry[i]) != lowerc((unsigned char)sender[i])) return false;
  }
  return true;
}

// channel key text -> bytes (same rules as 'channel set'). returns 16 or 32, or -1 if invalid. tmp must hold 32 bytes
static int parseChanKey(const char* key, uint8_t* tmp) {
  size_t kl = strlen(key);
  bool all_hex = kl > 0;
  for (size_t i = 0; i < kl; i++) {
    if (hexNibble(key[i]) < 0) all_hex = false;
  }
  int len;
  if ((kl == 32 || kl == 64) && all_hex) {
    len = (int)(kl / 2);
    for (int i = 0; i < len; i++) {
      tmp[i] = (uint8_t)((hexNibble(key[i * 2]) << 4) | hexNibble(key[i * 2 + 1]));
    }
  } else {
    len = b64Decode(key, tmp, 32);
  }
  return (len == 16 || len == 32) ? len : -1;
}

uint8_t MyMesh::blockNameCount() const {
  uint8_t n = 0;
  for (int i = 0; i < MAX_BLOCKNAME; i++) {
    if (blockname[i].name[0]) n++;
  }
  return n;
}

// text is "<sender>: <message>" from the Public channel or a filter channel
void MyMesh::blockNameCheckText(const char* text) {
  const char* sep = strstr(text, ": ");
  if (sep == NULL) return;
  int slen = (int)(sep - text);
  if (slen <= 0 || slen >= BLOCKNAME_LEN) return;
  for (int i = 0; i < MAX_BLOCKNAME; i++) {
    BlockNameEntry& e = blockname[i];
    if (e.name[0] == 0 || !nameMatches(e.name, text, slen)) continue;
    e.hits++;
    e.last_hit_ms = millis();
    block_name_hit = true;   // allowPacketForward() refuses to relay this packet
    return;
  }
}

// one line per page: "Spam123* x12 5m | Free Crypto x3 2h | Bot42 x0 - p1/2"; a page is filled as far as the reply fits
void MyMesh::formatBlockNameList(char* reply, int page) {
  if (blockNameCount() == 0) {
    strcpy(reply, "blockname: list empty");
    return;
  }
  int limit = CLI_REPLY_MAX - 8;   // room for " pN/M"
  if (from_channel) {              // over the channel the repeater's name is added in front, and the message is cut at CHAN_MSG_MAX
    int prefix = (int)strlen(_prefs.node_name) + 2;
    if (prefix > 39) prefix = 39;
    limit = CHAN_MSG_MAX - prefix - 8;
  }
  unsigned long now = millis();
  int pages = 1;
  for (int pass = 0; pass < 2; pass++) {   // pass 0 counts the pages, pass 1 writes the wanted one
    int cur_page = 1, in_page = 0, cur_len = 0;
    size_t pos = 0;
    reply[0] = 0;
    for (int i = 0; i < MAX_BLOCKNAME; i++) {
      const BlockNameEntry& e = blockname[i];
      if (e.name[0] == 0) continue;
      char age[12];
      if (e.hits) fmtAge(age, sizeof(age), now - e.last_hit_ms);
      else strcpy(age, "-");
      char item[72];
      int il = snprintf(item, sizeof(item), "%s x%lu %s", e.name, (unsigned long)e.hits, age);
      if (il >= (int)sizeof(item)) il = (int)sizeof(item) - 1;
      int need = il + (in_page ? 3 : 0);   // " | " between entries
      if (in_page >= BLOCKNAME_PER_PAGE || (in_page > 0 && cur_len + need > limit)) {
        cur_page++;
        in_page = 0;
        cur_len = 0;
        need = il;
      }
      if (pass == 1 && cur_page == page) {
        if (in_page) {
          memcpy(&reply[pos], " | ", 4);
          pos += 3;
        }
        memcpy(&reply[pos], item, il + 1);
        pos += il;
      }
      cur_len += need;
      in_page++;
    }
    if (pass == 0) {
      pages = cur_page;
      if (page < 1) page = 1;
      if (page > pages) page = pages;
    } else if (pages > 1) {
      snprintf(&reply[pos], 12, " p%d/%d", page, pages);
    }
  }
}

// true if the relay whitelist is on and this is a channel message from a channel that is not allowed
bool MyMesh::chanAllowBlocks(const mesh::Packet* packet) {
  if (!chanallow_on || !packet->isRouteFlood()) return false;
  uint8_t t = packet->getPayloadType();
  if (t != PAYLOAD_TYPE_GRP_TXT && t != PAYLOAD_TYPE_GRP_DATA) return false;   // adverts, direct messages, acks ...: relayed as always
  if (packet->payload_len < 1) return false;
  uint8_t h = packet->payload[0];   // channel hash, sent in the clear
  if (chan_key_set && h == chan.hash[0]) return false;   // the command channel is always allowed
  if (h == public_chan.hash[0]) return false;            // so is Public
  if (chanallow_extra_set && h == chanallow_extra_hash) return false;
  chanallow_dropped++;
  return true;
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
  int pos = snprintf(reply, 32, "Bat %u.%02uV %d%%", mv / 1000, (mv % 1000) / 10, battPercent(mv));
  if (!isnan(t)) {
    int t10 = (int)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f));
    int a = t10 < 0 ? -t10 : t10;
    pos += snprintf(&reply[pos], 16, ", %s%d.%dC", t10 < 0 ? "-" : "", a / 10, a % 10);
  }
  char hops[8];   // hops the channel command took to reach us ('-' when the command did not come over the channel)
  if (cmd_hops >= 0) snprintf(hops, sizeof(hops), "%d", (int)cmd_hops);
  else strcpy(hops, "-");
  int w = snprintf(&reply[pos], CLI_REPLY_MAX - pos, ", cycle %s, auto %s, blocked %d, key %s, hops %s, up %lud%luh%lum",
                   cycle_active ? "on" : "off", sleep_auto_enabled ? "on" : "off", blocked,
                   chan_key_set ? "set" : "not set", hops, up / 86400UL, (up % 86400UL) / 3600UL, (up % 3600UL) / 60UL);
  if (bot_key_set && w > 0 && pos + w < CLI_REPLY_MAX - 10) {   // trail bot (item 32): shown only when a bot key is set
    strcat(reply, bot_on ? ", bot on" : ", bot off");
  }
  int sl = (int)strlen(reply);
  int tcount = testCount();
  if (tcount > 0 && sl < CLI_REPLY_MAX - 16) {   // test channels: shown only when a test key is set
    char tb[16];
    snprintf(tb, sizeof(tb), ", test %s x%d", test_on ? "on" : "off", tcount);
    strcat(reply, tb);
  }
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

// ---------------------- trail info bot ('botchan', item 32) ----------------------

// true if ch is the command channel (same key, or same 1 byte hash): the bot must never share it
bool MyMesh::botSameAsCommandChannel(const mesh::GroupChannel& ch) const {
  if (!chan_key_set) return false;
  return memcmp(ch.secret, chan.secret, sizeof(chan.secret)) == 0 || ch.hash[0] == chan.hash[0];
}

// why `ch` cannot be the key of bot channel `which` (BOT_CH_BOT / BOT_CH_TEST), or NULL if it is fine. The key (or its 1 byte
// hash) must differ from Public, the command channel, the bot channel and the other test channels. skip_slot = the test slot
// being replaced (it may match itself), or -1.
const char* MyMesh::botKeyConflict(const mesh::GroupChannel& ch, uint8_t which, int skip_slot) const {
  const bool is_test = which == BOT_CH_TEST;
  if (memcmp(ch.secret, public_chan.secret, sizeof(ch.secret)) == 0) return "Err - not the Public key";
  if (botSameAsCommandChannel(ch)) {
    return is_test ? "Err - test key (or its hash) matches the command channel"
                   : "Err - bot key (or its hash) matches the command channel";
  }
  if (is_test) {
    if (bot_key_set && (memcmp(ch.secret, bot_chan.secret, sizeof(ch.secret)) == 0 || ch.hash[0] == bot_chan.hash[0]))
      return "Err - test key (or its hash) matches the bot channel";
    for (int i = 0; i < TEST_CH_MAX; i++) {
      if (i == skip_slot || !test_slot_set[i]) continue;
      if (memcmp(ch.secret, test_chans[i].secret, sizeof(ch.secret)) == 0 || ch.hash[0] == test_chans[i].hash[0])
        return "Err - test key (or its hash) matches another test channel";
    }
  } else {
    for (int i = 0; i < TEST_CH_MAX; i++) {
      if (!test_slot_set[i]) continue;
      if (memcmp(ch.secret, test_chans[i].secret, sizeof(ch.secret)) == 0 || ch.hash[0] == test_chans[i].hash[0])
        return "Err - bot key (or its hash) matches a test channel";
    }
  }
  return NULL;
}

int MyMesh::testCount() const {
  int n = 0;
  for (int i = 0; i < TEST_CH_MAX; i++) {
    if (test_slot_set[i]) n++;
  }
  return n;
}

// /bot_key: 1 byte key length + 32 bytes key (same layout as /chan_key)
void MyMesh::saveBotKey() {
  if (_fs == NULL || !bot_key_set) return;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(BOT_KEY_FILE);
  File f = _fs->open(BOT_KEY_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = _fs->open(BOT_KEY_FILE, "w");
#else
  File f = _fs->open(BOT_KEY_FILE, "w", true);
#endif
  if (f) {
    uint8_t buf[33];
    buf[0] = bot_key_len;
    memcpy(&buf[1], bot_chan.secret, 32);
    f.write(buf, sizeof(buf));
    f.close();
  }
}

void MyMesh::loadBotKey() {
  if (_fs == NULL || !_fs->exists(BOT_KEY_FILE)) return;
#if defined(RP2040_PLATFORM)
  File f = _fs->open(BOT_KEY_FILE, "r");
#else
  File f = _fs->open(BOT_KEY_FILE);
#endif
  if (f) {
    uint8_t buf[33];
    int n = f.read(buf, sizeof(buf));
    f.close();
    if (n == 33 && (buf[0] == 16 || buf[0] == 32)) {
      mesh::GroupChannel tmp;
      memset(&tmp, 0, sizeof(tmp));
      memcpy(tmp.secret, &buf[1], 32);
      mesh::Utils::sha256(tmp.hash, sizeof(tmp.hash), tmp.secret, buf[0]);
      if (botKeyConflict(tmp, BOT_CH_BOT, -1) != NULL) return;   // never share the command channel / Public (e.g. a key was changed later)
      bot_chan = tmp;
      bot_key_len = buf[0];
      bot_key_set = true;
    }
  }
}

// /test_key: TEST_CH_MAX records of 33 bytes (1 byte key length, 0 = slot empty, then 32 bytes key). The first record is the
// same layout the single-channel version used, so an older /test_key loads into slot 1.
void MyMesh::saveTestKeys() {
  if (_fs == NULL) return;
  if (testCount() == 0) {
    _fs->remove(TEST_KEY_FILE);
    return;
  }
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(TEST_KEY_FILE);
  File f = _fs->open(TEST_KEY_FILE, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File f = _fs->open(TEST_KEY_FILE, "w");
#else
  File f = _fs->open(TEST_KEY_FILE, "w", true);
#endif
  if (f) {
    uint8_t buf[33 * TEST_CH_MAX];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < TEST_CH_MAX; i++) {
      if (!test_slot_set[i]) continue;
      buf[i * 33] = test_key_len[i];
      memcpy(&buf[i * 33 + 1], test_chans[i].secret, 32);
    }
    f.write(buf, sizeof(buf));
    f.close();
  }
}

void MyMesh::loadTestKeys() {
  if (_fs == NULL || !_fs->exists(TEST_KEY_FILE)) return;
#if defined(RP2040_PLATFORM)
  File f = _fs->open(TEST_KEY_FILE, "r");
#else
  File f = _fs->open(TEST_KEY_FILE);
#endif
  if (f) {
    uint8_t buf[33 * TEST_CH_MAX];
    int n = f.read(buf, sizeof(buf));
    f.close();
    for (int i = 0; i < TEST_CH_MAX && (i + 1) * 33 <= n; i++) {
      const uint8_t* rec = &buf[i * 33];
      if (rec[0] != 16 && rec[0] != 32) continue;   // empty slot
      mesh::GroupChannel tmp;
      memset(&tmp, 0, sizeof(tmp));
      memcpy(tmp.secret, &rec[1], 32);
      mesh::Utils::sha256(tmp.hash, sizeof(tmp.hash), tmp.secret, rec[0]);
      if (botKeyConflict(tmp, BOT_CH_TEST, i) != NULL) continue;   // never share the command channel / Public / the bot channel / another test channel
      test_chans[i] = tmp;
      test_key_len[i] = rec[0];
      test_slot_set[i] = true;
    }
  }
}

// ---- reply queue: up to BOT_ANS_MAX answers (each up to BOT_Q_MAX messages) shared by the bot channel and the #test channel ----

void MyMesh::botQueueClear() {
  bot_q_n = 0;
  bot_ans_n = 0;
}

// drop the waiting answers of channels ch_lo to ch_hi (switched off or key cleared); the other channels' answers stay
void MyMesh::botQueueDropChannel(uint8_t ch_lo, uint8_t ch_hi) {
  int src = 0, dst = 0, dst_ans = 0;
  for (int a = 0; a < bot_ans_n; a++) {
    int len = bot_ans_len[a];
    if (bot_ans_ch[a] < ch_lo || bot_ans_ch[a] > ch_hi) {
      for (int i = 0; i < len; i++, dst++) {
        if (dst != src + i) strcpy(bot_q[dst], bot_q[src + i]);
      }
      bot_ans_len[dst_ans] = (uint8_t)len;
      bot_ans_ch[dst_ans] = bot_ans_ch[a];
      bot_ans_who[dst_ans] = bot_ans_who[a];
      dst_ans++;
    }
    src += len;
  }
  bot_q_n = (uint8_t)dst;
  bot_ans_n = (uint8_t)dst_ans;
}

// an answer of n messages was just written at bot_q[bot_q_n]: queue it. `spread` = first reply 3 to 15 s from now (random, so bot
// repeaters do not collide); an answer queued behind others just follows them at the normal 10 s gap.
void MyMesh::botCommit(uint8_t ch, uint16_t who, int n, bool spread) {
  bool was_empty = bot_q_n == 0;
  bot_ans_len[bot_ans_n] = (uint8_t)n;
  bot_ans_ch[bot_ans_n] = ch;
  bot_ans_who[bot_ans_n] = who;
  bot_ans_n++;
  bot_q_n = (uint8_t)(bot_q_n + n);
  if (was_empty) {
    unsigned long at = futureMillis(spread ? getRNG()->nextInt(BOT_FIRST_MIN_MS, BOT_FIRST_MAX_MS + 1) : 0);
    // never closer than the 10 s gap to the previous message
    if (!millisHasNowPassed(bot_next_send_at) && (long)(bot_next_send_at - at) > 0) at = bot_next_send_at;
    bot_next_send_at = at;
  }
}

// true while this person still has an answer waiting, or sent their last answer less than 10 s ago
bool MyMesh::botUserBlocked(uint16_t id) const {
  for (int a = 0; a < bot_ans_n; a++) {
    if (bot_ans_who[a] == id) return true;
  }
  for (int i = 0; i < BOT_CD_MAX; i++) {
    if (bot_cd[i].id == id && !millisHasNowPassed(bot_cd[i].ready_at)) return true;
  }
  return false;
}

// this person's answer is complete: 10 s before their next request is answered
void MyMesh::botUserDone(uint16_t id) {
  if (id == 0) return;
  int pick = -1;
  for (int i = 0; i < BOT_CD_MAX; i++) {
    if (bot_cd[i].id == id) { pick = i; break; }
  }
  if (pick < 0) {
    for (int i = 0; i < BOT_CD_MAX; i++) {   // a free or expired entry
      if (bot_cd[i].id == 0 || millisHasNowPassed(bot_cd[i].ready_at)) { pick = i; break; }
    }
  }
  if (pick < 0) {   // all in use: replace the one that ends first
    pick = 0;
    for (int i = 1; i < BOT_CD_MAX; i++) {
      if ((long)(bot_cd[i].ready_at - bot_cd[pick].ready_at) < 0) pick = i;
    }
  }
  bot_cd[pick].id = id;
  bot_cd[pick].ready_at = futureMillis(BOT_USER_COOLDOWN_MS);
}

// a message heard on the bot channel or the #test channel: "<sender>: !<trail, town, 'trails' or 'test'>".
// Only called while that channel is on and the radio is awake.
// hops / snr / rssi describe how this repeater heard the message ('!test' reports them; rssi is the radio's last reading).
// ch = BOT_CH_BOT: trails, hiking help, '!test' and chat. ch >= BOT_CH_TEST (a test channel): only '!test', '!ping' and the radio-check phrases.
void MyMesh::handleBotMessage(const char* text, int hops, float snr, int rssi, uint8_t ch) {
  const char* body = strstr(text, ": ");   // "<sender>: <message>"
  if (body == NULL) return;
  int slen = (int)(body - text);           // length of the sender name
  body += 2;
  while (*body == ' ') body++;
  if (*body != '!') return;                                         // '!' is the only trigger
  if (bot_ans_n >= BOT_ANS_MAX) return;                             // the queue is full: this request is ignored
  char nm[CHAT_NAME_MAX + 8];
  chatCleanName(text, slen, nm);
  uint16_t who = chatHash(nm, (int)strlen(nm));                     // the same id the bot's memory of people uses
  if (botUserBlocked(who)) return;                                  // this person: one answer per 10 s at most
  HikeCtx hc;
  hc.sender = text;                                                 // the sender's name is the text before the ": "
  hc.slen = slen;
  hc.node_name = _prefs.node_name;
  uint32_t now = getRTCClock()->getCurrentTime();
  hc.now_utc = now < CLOCK_VALID_AFTER ? 0 : now;                   // 0 = clock not set
  hc.snr10 = (int)(snr * 10.0f + (snr >= 0 ? 0.5f : -0.5f));
  hc.rssi = rssi;
  hc.hops = hops;
  hc.seed = (uint32_t)getRNG()->nextInt(0, 65536);
  hc.test_only = (ch != BOT_CH_BOT);
  // the reply is written straight after the messages already waiting; there is always room for BOT_Q_MAX more while
  // fewer than BOT_ANS_MAX answers wait (an answer is never longer than BOT_Q_MAX messages)
  int n = hikebotReply(&hc, body + 1, bot_q + bot_q_n);
  if (n <= 0) return;
  if (n > BOT_Q_MAX) n = BOT_Q_MAX;
  botCommit(ch, who, n, true);
  if (ch != BOT_CH_BOT) test_replies++;
  else bot_replies++;
}

// build and flood a text message on the bot channel (ch = BOT_CH_BOT) or a test channel (BOT_CH_TEST + slot), the same way
// postChannel() does but with that channel's key and the bot length limit
bool MyMesh::postBotMessage(const char* text, uint8_t ch) {
  const bool is_test = ch != BOT_CH_BOT;
  int slot = (int)ch - BOT_CH_TEST;
  if (is_test && (slot < 0 || slot >= TEST_CH_MAX)) return false;
  if (!(is_test ? test_slot_set[slot] : bot_key_set)) return false;
  const mesh::GroupChannel& kc = is_test ? test_chans[slot] : bot_chan;
  uint8_t temp[5 + BOT_MSG_MAX + 40];
  uint32_t ts = getRTCClock()->getCurrentTime();
  memcpy(temp, &ts, 4);
  temp[4] = 0;   // TXT_TYPE_PLAIN
  int prefix_len = snprintf((char*)&temp[5], 40, "%s: ", _prefs.node_name);   // <sender>: <msg>
  if (prefix_len > 39) prefix_len = 39;
  int text_len = (int)strlen(text);
  if (prefix_len + text_len > BOT_MSG_MAX) text_len = BOT_MSG_MAX - prefix_len;
  memcpy(&temp[5 + prefix_len], text, text_len);
  temp[5 + prefix_len + text_len] = 0;
  mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, kc, temp, 5 + prefix_len + text_len);
  if (pkt == NULL) return false;
  sendFloodScoped(default_scope, pkt, 0, _prefs.path_hash_mode + 1);
  return true;
}

void MyMesh::botService() {   // post the queued bot messages, one at a time, only while the radio is awake
  if (bot_q_n == 0) return;
  uint8_t ch = bot_ans_ch[0];
  bool key_ok = ch == BOT_CH_BOT ? bot_key_set
                : ((int)ch - BOT_CH_TEST < TEST_CH_MAX && test_slot_set[(int)ch - BOT_CH_TEST]);
  if (!key_ok) {   // (a 'say' may be queued while the channel is off; 'off', 'clear' and sleep empty the queue themselves)
    botQueueDropChannel(ch, ch);
    return;
  }
  if (!millisHasNowPassed(bot_next_send_at)) return;
  char msg[BOT_MSG_MAX + 1];
  strcpy(msg, bot_q[0]);
  for (int i = 1; i < bot_q_n; i++) strcpy(bot_q[i - 1], bot_q[i]);
  bot_q_n--;
  postBotMessage(msg, ch);
  bot_next_send_at = futureMillis(BOT_GAP_MS);
  if (--bot_ans_len[0] == 0) {   // that answer is complete: its sender waits 10 s, the next answer moves up
    botUserDone(bot_ans_who[0]);
    for (int a = 1; a < bot_ans_n; a++) {
      bot_ans_len[a - 1] = bot_ans_len[a];
      bot_ans_ch[a - 1] = bot_ans_ch[a];
      bot_ans_who[a - 1] = bot_ans_who[a];
    }
    bot_ans_n--;
  }
}

int MyMesh::searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) {
  int n = 0;
  if (chan_key_set && n < max_matches && chan.hash[0] == hash[0]) {   // the private command channel
    channels[n++] = chan;
  }
  if (bot_key_set && bot_on && n < max_matches && bot_chan.hash[0] == hash[0]) {   // the trail bot channel (read for "!" requests only)
    channels[n++] = bot_chan;
  }
  for (int i = 0; test_on && i < TEST_CH_MAX; i++) {   // the test channels (read for "!test" requests only)
    if (test_slot_set[i] && n < max_matches && test_chans[i].hash[0] == hash[0]) channels[n++] = test_chans[i];
  }
  if (blockNameCount() == 0) return n;   // empty blockname list: Public / filter channels are not decrypted at all
  if (n < max_matches && public_chan.hash[0] == hash[0]) {
    channels[n++] = public_chan;
  }
  for (int i = 0; i < MAX_FILTER_KEYS && n < max_matches; i++) {
    const FilterKey& k = filter_keys[i];
    if (!k.set || k.ch.hash[0] != hash[0]) continue;
    bool dup = false;   // same key as one already offered (command, Public or another slot): don't decrypt twice
    for (int j = 0; j < n; j++) {
      if (memcmp(channels[j].secret, k.ch.secret, sizeof(k.ch.secret)) == 0) dup = true;
    }
    if (!dup) channels[n++] = k.ch;
  }
  return n;
}

void MyMesh::onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) {
  if (type != PAYLOAD_TYPE_GRP_TXT || len < 6) return;
  if ((data[4] >> 2) != 0) return;   // plain text only
  uint32_t ts;
  memcpy(&ts, data, 4);
  data[len] = 0;   // text is padded with zeroes (same as BaseChatMesh::onGroupDataRecv)
  bool is_command_channel = chan_key_set && memcmp(channel.secret, chan.secret, sizeof(chan.secret)) == 0;
  if (!is_command_channel && bot_key_set && memcmp(channel.secret, bot_chan.secret, sizeof(bot_chan.secret)) == 0) {
    if (bot_on) {   // the trail bot channel: "!" requests only, NEVER commands
      handleBotMessage((const char*)&data[5], (int)packet->getPathHashCount(), packet->getSNR(), (int)radio_driver.getLastRSSI(), BOT_CH_BOT);
    }
    return;
  }
  for (int i = 0; !is_command_channel && i < TEST_CH_MAX; i++) {
    if (test_slot_set[i] && memcmp(channel.secret, test_chans[i].secret, sizeof(channel.secret)) == 0) {
      if (test_on) {   // a test channel: "!test" requests only, NEVER commands
        handleBotMessage((const char*)&data[5], (int)packet->getPathHashCount(), packet->getSNR(), (int)radio_driver.getLastRSSI(), BOT_CH_TEST + i);
      }
      return;
    }
  }
  if (!is_command_channel) {   // Public or a filter channel: only check the sender name, NEVER run commands
    blockNameCheckText((const char*)&data[5]);
    return;
  }
  handleChannelCommand((const char*)&data[5], ts, (int)packet->getPathHashCount());
}

// channel key text -> bytes for 'botchan set' / 'testchan set': a hashtag channel name ("#test") gives the key the apps
// use for that channel (first 16 bytes of the SHA-256 of the lower case name); anything else is a base64 / hex key like
// 'channel set'. Returns 16 or 32, or -1 if invalid. tmp must hold 32 bytes.
// (Only the bot channels accept '#name': it is a public key, so never use it for the private command channel.)
static int parseBotKey(const char* key, uint8_t* tmp) {
  if (key[0] != '#') return parseChanKey(key, tmp);
  char name[33];
  size_t kl = strlen(key);
  if (kl < 2 || kl > 31) return -1;
  for (size_t i = 0; i < kl; i++) {
    char c = key[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    bool ok = (i == 0) ? (c == '#') : ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-');
    if (!ok) return -1;
    name[i] = c;
  }
  name[kl] = 0;
  mesh::Utils::sha256(tmp, 16, (const uint8_t*)name, (int)kl);
  return 16;
}

// 'botchan': [get] | set <key or #name> | clear | on | off | say <text>. sub = what follows the command word. Writes the reply.
void MyMesh::botChanCommand(const char* sub, char* reply) {
  bool is_get = strcmp(sub, "get") == 0;
  if (*sub == 0 || is_get) {
    if (!bot_key_set) {
      strcpy(reply, "botchan key not set");
    } else if (is_get) {   // shows the key (over the command channel too: only command channel members can read it)
      char b64[48];
      b64Encode(bot_chan.secret, bot_key_len, b64);
      snprintf(reply, 120, "botchan %s, hash %02X, replies %lu, key: %s", bot_on ? "on" : "off", bot_chan.hash[0], (unsigned long)bot_replies, b64);
    } else {
      snprintf(reply, 80, "botchan %s, hash %02X, replies %lu", bot_on ? "on" : "off", bot_chan.hash[0], (unsigned long)bot_replies);
    }
  } else if (strcmp(sub, "on") == 0) {
    if (!bot_key_set) {
      strcpy(reply, "Err - botchan key not set (botchan set <key or #name>)");
    } else {
      bot_on = true;
      strcpy(reply, "OK - botchan on");
    }
  } else if (strcmp(sub, "off") == 0) {
    bot_on = false;
    botQueueDropChannel(BOT_CH_BOT, BOT_CH_BOT);   // the bot channel's waiting replies go; the test channels' stay
    strcpy(reply, "OK - botchan off");
  } else if (strcmp(sub, "say") == 0 || memcmp(sub, "say ", 4) == 0) {   // post a text to the bot channel
    const char* txt = sub + 3;
    while (*txt == ' ') txt++;
    int prefix = (int)strlen(_prefs.node_name) + 2;   // the channel shows "<node name>: <text>"
    if (prefix > 39) prefix = 39;
    int maxlen = BOT_MSG_MAX - prefix;
    if (!bot_key_set) {
      strcpy(reply, "Err - botchan key not set (botchan set <key or #name>)");
    } else if (*txt == 0) {
      strcpy(reply, "Err - botchan say <text>");
    } else if (bot_ans_n >= BOT_ANS_MAX) {
      strcpy(reply, "Err - bot busy, try again in a moment");
    } else {
      int tl = (int)strlen(txt);
      if (tl > maxlen) {
        tl = maxlen;
        while (tl > 0 && (((unsigned char)txt[tl]) & 0xC0) == 0x80) tl--;   // never cut an accented letter / emoji in half
      }
      memcpy(bot_q[bot_q_n], txt, tl);
      bot_q[bot_q_n][tl] = 0;
      // over the command channel (a command for 'all' reaches every repeater) the post is spread out at random; from the CLI it goes at once
      botCommit(BOT_CH_BOT, 0, 1, from_channel);
      strcpy(reply, tl < (int)strlen(txt) ? "OK - botchan say queued (cut to fit)" : "OK - botchan say queued");
    }
  } else if (strcmp(sub, "clear") == 0) {
    bot_key_set = false;
    bot_on = false;
    bot_key_len = 0;
    memset(&bot_chan, 0, sizeof(bot_chan));
    botQueueDropChannel(BOT_CH_BOT, BOT_CH_BOT);
    if (_fs) _fs->remove(BOT_KEY_FILE);
    strcpy(reply, "OK - botchan key cleared");
  } else if (memcmp(sub, "set ", 4) == 0) {
    const char* key = sub + 4;
    while (*key == ' ') key++;
    uint8_t tmpk[32];
    int len = parseBotKey(key, tmpk);
    if (len < 0) {
      strcpy(reply, "Err - key must be #name, or base64 / hex, 16 or 32 bytes");
    } else {
      mesh::GroupChannel tc;
      memset(&tc, 0, sizeof(tc));
      memcpy(tc.secret, tmpk, len);
      mesh::Utils::sha256(tc.hash, sizeof(tc.hash), tc.secret, len);
      const char* err = botKeyConflict(tc, BOT_CH_BOT, -1);
      if (err != NULL) {
        strcpy(reply, err);
      } else {
        bot_chan = tc;
        bot_key_len = (uint8_t)len;
        bot_key_set = true;
        saveBotKey();
        snprintf(reply, 100, "OK - bot key set and saved (hash %02X), bot is %s", bot_chan.hash[0], bot_on ? "on" : "off");
      }
    }
  } else {
    strcpy(reply, "Err - botchan [get]|set <key or #name>|clear|on|off|say <text>");
  }
}

// 'testchan': up to TEST_CH_MAX test channels (slots 1 to 3), one on/off switch for all of them. A test channel answers only
// '!test', '!ping' and the radio-check phrases.
//   testchan                 state, how many channels, their hashes
//   testchan get [n]         the same / the key of channel n
//   testchan set [n] <key or #name>   adds a channel in the first free slot (or puts it in slot n, replacing that one)
//   testchan remove <n or key or #name>, clear (all), on, off, say <text> (posts on every test channel)
void MyMesh::testChanCommand(const char* sub, char* reply) {
  int cnt = testCount();
  bool is_get = strcmp(sub, "get") == 0 || memcmp(sub, "get ", 4) == 0;
  if (*sub == 0 || is_get) {
    if (cnt == 0) {
      strcpy(reply, "testchan key not set");
    } else if (is_get && sub[3] == ' ' && sub[4] >= '1' && sub[4] <= '0' + TEST_CH_MAX && sub[5] == 0) {   // testchan get n: shows the key
      int i = sub[4] - '1';
      if (!test_slot_set[i]) {
        snprintf(reply, 40, "testchan %d: empty", i + 1);
      } else {
        char b64[48];
        b64Encode(test_chans[i].secret, test_key_len[i], b64);
        snprintf(reply, 120, "testchan %d: hash %02X, key: %s", i + 1, test_chans[i].hash[0], b64);
      }
    } else {
      int w = snprintf(reply, 60, "testchan %s, %d of %d,", test_on ? "on" : "off", cnt, TEST_CH_MAX);
      for (int i = 0; i < TEST_CH_MAX; i++) {
        if (test_slot_set[i] && w < 100) w += snprintf(reply + w, 20, " %d:%02X", i + 1, test_chans[i].hash[0]);
      }
      snprintf(reply + w, 30, ", replies %lu", (unsigned long)test_replies);
    }
  } else if (strcmp(sub, "on") == 0) {
    if (cnt == 0) {
      strcpy(reply, "Err - testchan key not set (testchan set <key or #name>)");
    } else {
      test_on = true;
      strcpy(reply, "OK - testchan on");
    }
  } else if (strcmp(sub, "off") == 0) {
    test_on = false;
    botQueueDropChannel(BOT_CH_TEST, BOT_CH_TEST + TEST_CH_MAX - 1);   // the test channels' waiting replies go; the bot channel's stay
    strcpy(reply, "OK - testchan off");
  } else if (strcmp(sub, "say") == 0 || memcmp(sub, "say ", 4) == 0) {   // post a text on every test channel
    const char* txt = sub + 3;
    while (*txt == ' ') txt++;
    int prefix = (int)strlen(_prefs.node_name) + 2;   // the channel shows "<node name>: <text>"
    if (prefix > 39) prefix = 39;
    int maxlen = BOT_MSG_MAX - prefix;
    if (cnt == 0) {
      strcpy(reply, "Err - testchan key not set (testchan set <key or #name>)");
    } else if (*txt == 0) {
      strcpy(reply, "Err - testchan say <text>");
    } else if (bot_ans_n + cnt > BOT_ANS_MAX) {
      strcpy(reply, "Err - bot busy, try again in a moment");
    } else {
      int tl = (int)strlen(txt);
      if (tl > maxlen) {
        tl = maxlen;
        while (tl > 0 && (((unsigned char)txt[tl]) & 0xC0) == 0x80) tl--;   // never cut an accented letter / emoji in half
      }
      for (int i = 0; i < TEST_CH_MAX; i++) {
        if (!test_slot_set[i]) continue;
        memcpy(bot_q[bot_q_n], txt, tl);
        bot_q[bot_q_n][tl] = 0;
        botCommit((uint8_t)(BOT_CH_TEST + i), 0, 1, from_channel);
      }
      strcpy(reply, tl < (int)strlen(txt) ? "OK - testchan say queued (cut to fit)" : "OK - testchan say queued");
    }
  } else if (strcmp(sub, "clear") == 0) {
    memset(test_slot_set, 0, sizeof(test_slot_set));
    memset(test_key_len, 0, sizeof(test_key_len));
    memset(test_chans, 0, sizeof(test_chans));
    test_on = false;
    botQueueDropChannel(BOT_CH_TEST, BOT_CH_TEST + TEST_CH_MAX - 1);
    if (_fs) _fs->remove(TEST_KEY_FILE);
    strcpy(reply, "OK - testchan keys cleared");
  } else if (memcmp(sub, "remove ", 7) == 0) {
    const char* arg = sub + 7;
    while (*arg == ' ') arg++;
    int slot = -1;
    if (arg[0] >= '1' && arg[0] <= '0' + TEST_CH_MAX && arg[1] == 0) {
      slot = arg[0] - '1';
    } else {
      uint8_t tmpk[32];
      int len = parseBotKey(arg, tmpk);
      for (int i = 0; len > 0 && i < TEST_CH_MAX; i++) {
        if (test_slot_set[i] && test_key_len[i] == len && memcmp(test_chans[i].secret, tmpk, len) == 0) slot = i;
      }
    }
    if (slot < 0 || !test_slot_set[slot]) {
      strcpy(reply, "Err - no such test channel (testchan remove <1-3, key or #name>)");
    } else {
      test_slot_set[slot] = false;
      test_key_len[slot] = 0;
      memset(&test_chans[slot], 0, sizeof(test_chans[slot]));
      botQueueDropChannel((uint8_t)(BOT_CH_TEST + slot), (uint8_t)(BOT_CH_TEST + slot));
      if (testCount() == 0) test_on = false;
      saveTestKeys();
      snprintf(reply, 60, "OK - test channel %d removed", slot + 1);
    }
  } else if (memcmp(sub, "set ", 4) == 0) {
    const char* key = sub + 4;
    while (*key == ' ') key++;
    int slot = -1;   // -1: first free slot
    if (key[0] >= '1' && key[0] <= '0' + TEST_CH_MAX && key[1] == ' ') {   // testchan set <n> <key>: a chosen slot
      slot = key[0] - '1';
      key += 2;
      while (*key == ' ') key++;
    }
    uint8_t tmpk[32];
    int len = parseBotKey(key, tmpk);
    if (len < 0) {
      strcpy(reply, "Err - key must be #name, or base64 / hex, 16 or 32 bytes");
    } else {
      mesh::GroupChannel tc;
      memset(&tc, 0, sizeof(tc));
      memcpy(tc.secret, tmpk, len);
      mesh::Utils::sha256(tc.hash, sizeof(tc.hash), tc.secret, len);
      int same = -1;   // this key is already one of the test channels
      for (int i = 0; i < TEST_CH_MAX; i++) {
        if (test_slot_set[i] && memcmp(test_chans[i].secret, tc.secret, sizeof(tc.secret)) == 0) same = i;
      }
      if (slot < 0 && same >= 0) {
        snprintf(reply, 60, "OK - already test channel %d (hash %02X)", same + 1, tc.hash[0]);
        return;
      }
      if (slot < 0) {
        for (int i = 0; i < TEST_CH_MAX; i++) {
          if (!test_slot_set[i]) { slot = i; break; }
        }
      }
      if (slot < 0) {
        strcpy(reply, "Err - 3 test channels already set (testchan remove <1-3>)");
        return;
      }
      const char* err = botKeyConflict(tc, BOT_CH_TEST, slot);
      if (err != NULL) {
        strcpy(reply, err);
      } else {
        botQueueDropChannel((uint8_t)(BOT_CH_TEST + slot), (uint8_t)(BOT_CH_TEST + slot));   // (a key that is replaced)
        test_chans[slot] = tc;
        test_key_len[slot] = (uint8_t)len;
        test_slot_set[slot] = true;
        saveTestKeys();
        snprintf(reply, 100, "OK - test channel %d set and saved (hash %02X), test is %s", slot + 1, tc.hash[0], test_on ? "on" : "off");
      }
    }
  } else {
    strcpy(reply, "Err - testchan [get n]|set [n] <key or #name>|remove <n>|clear|on|off|say <text>");
  }
}

// channel commands look like  <battlog all>  <templog Tuna>  <sleep until 21:00 all>
void MyMesh::handleChannelCommand(const char* text, uint32_t ts, int hops) {
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
  bool to_all = endsWithTarget(buf, inner, "all", &cmd_len);
  if (!to_all && !endsWithTarget(buf, inner, _prefs.node_name, &cmd_len)) return;
  while (cmd_len > 0 && buf[cmd_len - 1] == ' ') cmd_len--;
  buf[cmd_len] = 0;

  bool timed_sleep = (strncmp(buf, "sleep until ", 12) == 0) || (strncmp(buf, "sleep ", 6) == 0 && buf[6] >= '0' && buf[6] <= '9');
  bool secret_get = false;   // never hand out the private key or passwords over the channel
  if (strncmp(buf, "get ", 4) == 0) {
    for (const char* p = buf + 4; *p; p++) {
      if (strncmp(p, "prv", 3) == 0 || strncmp(p, "password", 8) == 0 || strncmp(p, "secret", 6) == 0) secret_get = true;
    }
  }
  bool allowed = timed_sleep || strcmp(buf, "battlog") == 0 || strcmp(buf, "battpct") == 0 || strcmp(buf, "templog") == 0
                 || strcmp(buf, "status") == 0 || strcmp(buf, "report") == 0 || strcmp(buf, "advert") == 0
                 || strcmp(buf, "ver") == 0 || strcmp(buf, "clock") == 0   // read-only commands
                 || strcmp(buf, "stats-core") == 0 || strcmp(buf, "stats-radio") == 0 || strcmp(buf, "stats-packets") == 0
                 || (strncmp(buf, "get ", 4) == 0 && !secret_get)   // every 'get' command except the secrets
                 || strncmp(buf, "blockhop ", 9) == 0   // blockhop add / remove / list / clear
                 || strncmp(buf, "blockname ", 10) == 0   // blockname add / remove / list / clear
                 || strcmp(buf, "botchan") == 0 || strncmp(buf, "botchan ", 8) == 0   // trail bot: get / set / clear / on / off / say
                 || strcmp(buf, "testchan") == 0 || strncmp(buf, "testchan ", 9) == 0   // #test channel: get / set / clear / on / off / say
                 || strcmp(buf, "chanallow") == 0 || strncmp(buf, "chanallow ", 10) == 0   // chanallow on / off / set / clear
                 || strcmp(buf, "filterchan get") == 0 || strncmp(buf, "filterchan set ", 15) == 0   // (never 'filterchan get <slot>': it shows a key)
                 || strncmp(buf, "filterchan clear", 16) == 0
                 || strcmp(buf, "sleep cycle on") == 0 || strcmp(buf, "sleep cycle off") == 0
                 || strcmp(buf, "sleep auto on") == 0 || strcmp(buf, "sleep auto off") == 0;
  if (!allowed) return;   // only these commands can be given over the channel

  if (ts <= last_cmd_ts) return;   // replayed or duplicate command
  last_cmd_ts = ts;

  char reply[192];
  reply[0] = 0;
  from_channel = true;
  sleep_cmd_flag = false;   // startSleep() sets this if the command starts a sleep
  cmd_hops = hops;          // 'status' reports it
  if (strcmp(buf, "advert") == 0) {   // flood advert, delayed at random so repeaters don't all send at once
    sendSelfAdvertisement(getRNG()->nextInt(2000, 60001), true);
    strcpy(reply, "OK - advert sent");
  } else {
    handleCommand(0, buf, reply);   // (0 = no clock sync from channel messages)
  }
  from_channel = false;
  cmd_hops = -1;
  bool started_sleep = sleep_cmd_flag;
  sleep_cmd_flag = false;
  if (reply[0] != 0) {
    if ((reply[0] == 'V' || reply[0] == 'C' || reply[0] == '%') && reply[1] == '/') {   // battlog / battpct / templog line: make it fit
      int prefix = (int)strlen(_prefs.node_name) + 2;
      if (prefix > 39) prefix = 39;
      trimLogToFit(reply, 12, prefix);
    }
    chanEnqueue(reply, CHAN_GAP_MS);
  }
  chan_gap_on = true;
  if (to_all && started_sleep) {
    // a sleep command for 'all': the furthest repeaters answer (and sleep) first, so every reply can still be
    // relayed by nearer repeaters that are awake. 112 s at 0 hops, 10 s less per hop, plus a random 0..8 s.
    int32_t lo = (int32_t)STAGGER_BASE_S - (int32_t)STAGGER_STEP_S * (int32_t)hops;
    if (lo < STAGGER_FLOOR_S) lo = STAGGER_FLOOR_S;
    chan_next_send_at = futureMillis(getRNG()->nextInt(lo * 1000, (lo + STAGGER_SPREAD_S) * 1000 + 1));
  } else {
    // every repeater answers a command for 'all': spread the replies out by a random 5..60 seconds
    chan_next_send_at = futureMillis(getRNG()->nextInt(5000, 60001));
  }
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

  if (strcmp(command, "battpct") == 0) {
    formatBattPct(reply);
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
      uint8_t ctmp[32];
      int clen = parseChanKey(key, ctmp);
      bool is_bot_key = false, is_test_key = false;   // the command channel must differ from the trail bot and test channels
      if (clen > 0 && (bot_key_set || testCount() > 0)) {
        mesh::GroupChannel cc;
        memset(&cc, 0, sizeof(cc));
        memcpy(cc.secret, ctmp, clen);
        mesh::Utils::sha256(cc.hash, sizeof(cc.hash), cc.secret, clen);
        is_bot_key = bot_key_set && (memcmp(cc.secret, bot_chan.secret, sizeof(cc.secret)) == 0 || cc.hash[0] == bot_chan.hash[0]);
        for (int ti = 0; ti < TEST_CH_MAX; ti++) {
          if (test_slot_set[ti] && (memcmp(cc.secret, test_chans[ti].secret, sizeof(cc.secret)) == 0 || cc.hash[0] == test_chans[ti].hash[0]))
            is_test_key = true;
        }
      }
      if (is_bot_key) {
        strcpy(reply, "Err - that key (or its hash) is the trail bot channel");
      } else if (is_test_key) {
        strcpy(reply, "Err - that key (or its hash) is a test channel");
      } else if (setChannelKey(key)) {
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

  if (memcmp(command, "blockname", 9) == 0 && (command[9] == 0 || command[9] == ' ')) {
    const char* sub = command + 9;
    while (*sub == ' ') sub++;
    if (strcmp(sub, "list") == 0 || memcmp(sub, "list ", 5) == 0) {
      formatBlockNameList(reply, sub[4] == ' ' ? atoi(&sub[5]) : 1);
    } else if (strcmp(sub, "clear") == 0) {
      memset(blockname, 0, sizeof(blockname));
      strcpy(reply, "OK - blockname cleared");
    } else if (memcmp(sub, "add ", 4) == 0 || memcmp(sub, "remove ", 7) == 0) {
      bool is_add = (sub[0] == 'a');
      const char* name = sub + (is_add ? 4 : 7);
      while (*name == ' ') name++;
      size_t nl = strlen(name);
      if (nl == 0) {
        strcpy(reply, "Err - name missing");
      } else if (nl >= BLOCKNAME_LEN) {
        snprintf(reply, 40, "Err - name max %d chars", BLOCKNAME_LEN - 1);
      } else if (is_add) {
        if (strcmp(name, "*") == 0) {
          strcpy(reply, "Err - '*' alone would block everyone");
        } else {
          int free_idx = -1;
          bool listed = false;
          for (int i = 0; i < MAX_BLOCKNAME; i++) {
            if (blockname[i].name[0] == 0) {
              if (free_idx < 0) free_idx = i;
            } else if (eqNoCase(blockname[i].name, name)) {
              listed = true;
            }
          }
          if (listed) {
            strcpy(reply, "OK - already listed");
          } else if (free_idx < 0) {
            snprintf(reply, 40, "Err - blockname list full (%d)", MAX_BLOCKNAME);
          } else {
            memset(&blockname[free_idx], 0, sizeof(blockname[free_idx]));
            strncpy(blockname[free_idx].name, name, BLOCKNAME_LEN - 1);
            strcpy(reply, "OK - blocked");
          }
        }
      } else {
        int removed = 0;
        for (int i = 0; i < MAX_BLOCKNAME; i++) {
          if (blockname[i].name[0] && eqNoCase(blockname[i].name, name)) {
            memset(&blockname[i], 0, sizeof(blockname[i]));
            removed++;
          }
        }
        strcpy(reply, removed ? "OK - removed" : "Err - not in list");
      }
    } else {
      strcpy(reply, "Err - blockname add|remove|list|clear");
    }
    return true;
  }

  if (memcmp(command, "filterchan", 10) == 0 && (command[10] == 0 || command[10] == ' ')) {
    const char* sub = command + 10;
    while (*sub == ' ') sub++;
    if (*sub == 0 || strcmp(sub, "get") == 0) {   // slots with the channel hash, never the keys
      int pos = 0;
      reply[0] = 0;
      for (int i = 0; i < MAX_FILTER_KEYS; i++) {
        const FilterKey& k = filter_keys[i];
        if (k.set) pos += snprintf(&reply[pos], 40, "%s%d: set (hash %02X)", i ? " | " : "", i + 1, k.ch.hash[0]);
        else pos += snprintf(&reply[pos], 40, "%s%d: empty", i ? " | " : "", i + 1);
      }
      snprintf(&reply[pos], 40, " | Public: built in");
    } else if (memcmp(sub, "get ", 4) == 0) {    // full key of one slot: CLI only
      if (from_channel) {
        strcpy(reply, "Err - filterchan get <slot> is CLI only");
      } else {
        int slot = atoi(&sub[4]);
        if (slot < 1 || slot > MAX_FILTER_KEYS) {
          strcpy(reply, "Err - filterchan get <1-4>");
        } else if (!filter_keys[slot - 1].set) {
          snprintf(reply, 40, "%d: empty", slot);
        } else {
          char b64[48];
          b64Encode(filter_keys[slot - 1].ch.secret, filter_keys[slot - 1].len, b64);
          snprintf(reply, 100, "%d: key: %s", slot, b64);
        }
      }
    } else if (memcmp(sub, "set ", 4) == 0) {
      const char* p = sub + 4;
      while (*p == ' ') p++;
      int slot = atoi(p);
      while (*p >= '0' && *p <= '9') p++;
      while (*p == ' ') p++;
      if (slot < 1 || slot > MAX_FILTER_KEYS || *p == 0) {
        strcpy(reply, "Err - filterchan set <1-4> <key>");
      } else {
        uint8_t tmp[32];
        int len = parseChanKey(p, tmp);
        if (len < 0) {
          strcpy(reply, "Err - key must be base64 or hex, 16 or 32 bytes");
        } else {
          FilterKey& k = filter_keys[slot - 1];
          memset(&k, 0, sizeof(k));
          memcpy(k.ch.secret, tmp, len);
          mesh::Utils::sha256(k.ch.hash, sizeof(k.ch.hash), k.ch.secret, len);   // same as BaseChatMesh::addChannel()
          k.len = (uint8_t)len;
          k.set = true;
          snprintf(reply, 60, "OK - filterchan %d set (hash %02X)", slot, k.ch.hash[0]);   // never repeats the key
        }
      }
    } else if (memcmp(sub, "clear", 5) == 0 && (sub[5] == 0 || sub[5] == ' ')) {
      const char* a = sub + 5;
      while (*a == ' ') a++;
      if (strcmp(a, "all") == 0) {
        memset(filter_keys, 0, sizeof(filter_keys));
        strcpy(reply, "OK - filterchan keys cleared");
      } else {
        int slot = atoi(a);
        if (*a == 0 || slot < 1 || slot > MAX_FILTER_KEYS) {
          strcpy(reply, "Err - filterchan clear <1-4>|all");
        } else {
          memset(&filter_keys[slot - 1], 0, sizeof(filter_keys[slot - 1]));
          snprintf(reply, 40, "OK - filterchan %d cleared", slot);
        }
      }
    } else {
      strcpy(reply, "Err - filterchan get|set <slot> <key>|clear <slot>|all");
    }
    return true;
  }

  if (memcmp(command, "chanallow", 9) == 0 && (command[9] == 0 || command[9] == ' ')) {
    const char* sub = command + 9;
    while (*sub == ' ') sub++;
    if (*sub == 0) {
      char cmdh[8], extra[8];
      if (chan_key_set) snprintf(cmdh, sizeof(cmdh), "%02X", chan.hash[0]);
      else strcpy(cmdh, "-");
      if (chanallow_extra_set) snprintf(extra, sizeof(extra), "%02X", chanallow_extra_hash);
      else strcpy(extra, "-");
      snprintf(reply, 100, "chanallow %s, cmd %s, Public %02X, extra %s, dropped %lu", chanallow_on ? "on" : "off", cmdh,
               public_chan.hash[0], extra, (unsigned long)chanallow_dropped);
    } else if (strcmp(sub, "on") == 0 || strcmp(sub, "off") == 0) {
      chanallow_on = (sub[1] == 'n');
      strcpy(reply, chanallow_on ? "OK - chanallow on" : "OK - chanallow off");
    } else if (strcmp(sub, "clear") == 0) {
      chanallow_extra_set = false;
      chanallow_extra_hash = 0;
      strcpy(reply, "OK - extra channel cleared");
    } else if (memcmp(sub, "set ", 4) == 0) {
      const char* a = sub + 4;
      while (*a == ' ') a++;
      if (strlen(a) == 2 && hexNibble(a[0]) >= 0 && hexNibble(a[1]) >= 0) {   // a 2 character hash
        chanallow_extra_hash = (uint8_t)((hexNibble(a[0]) << 4) | hexNibble(a[1]));
        chanallow_extra_set = true;
        snprintf(reply, 60, "OK - extra channel hash %02X", chanallow_extra_hash);
      } else if (from_channel) {   // a key never goes over the channel
        strcpy(reply, "Err - over the channel give a 2 character hash");
      } else {
        uint8_t tmp[32];
        int len = parseChanKey(a, tmp);
        if (len < 0) {
          strcpy(reply, "Err - give a 2 character hash, or a base64/hex key (16 or 32 bytes)");
        } else {
          mesh::GroupChannel tmpc;
          memset(&tmpc, 0, sizeof(tmpc));
          memcpy(tmpc.secret, tmp, len);
          mesh::Utils::sha256(tmpc.hash, sizeof(tmpc.hash), tmpc.secret, len);
          chanallow_extra_hash = tmpc.hash[0];
          chanallow_extra_set = true;
          snprintf(reply, 60, "OK - extra channel hash %02X", chanallow_extra_hash);
        }
      }
    } else {
      strcpy(reply, "Err - chanallow on|off|set <hash or key>|clear");
    }
    return true;
  }

  // trail info bot (item 32): its own channel key. CLI: botchan [get] | set <key or #name> | clear | on | off
  //   also: botchan say <text>  (posts <text> to the bot channel, as this repeater)
  // all of them work over the command channel too, e.g. <botchan say hello all>  <botchan set <key> Tuna>
  // 'testchan' is the same for the #test channel (it answers only '!test', '!ping' and the radio-check phrases)
  if (memcmp(command, "botchan", 7) == 0 && (command[7] == 0 || command[7] == ' ')) {
    const char* sub = command + 7;
    while (*sub == ' ') sub++;
    botChanCommand(sub, reply);
    return true;
  }
  if (memcmp(command, "testchan", 8) == 0 && (command[8] == 0 || command[8] == ' ')) {
    const char* sub = command + 8;
    while (*sub == ' ') sub++;
    testChanCommand(sub, reply);
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

  if (boot_notice_pending) {   // 'Rebooted' notice 20 to 60 seconds after start-up (needs the saved channel key)
    if (!boot_notice_armed) {
      boot_notice_armed = true;
      boot_notice_at = futureMillis(getRNG()->nextInt(20000, 60001));
    } else if (millisHasNowPassed(boot_notice_at)) {
      boot_notice_pending = false;
      unsigned mv = board.getBattMilliVolts();
      char msg[CHAN_MSG_MAX + 1];
      snprintf(msg, sizeof(msg), "Rebooted, battery %u.%02uV", mv / 1000, (mv % 1000) / 10);
      chanEnqueue(msg, CHAN_GAP_MS);
    }
  }
  dailyReport();           // 24h report timer (counts while asleep too)
  blockNoticeService();    // announce newly auto-blocked repeaters
  if (!sleeping) chanService();   // post queued channel messages while the radio is awake
  if (!sleeping) botService();    // post queued trail bot replies while the radio is awake

  // A packet leaves the send queue when its transmission STARTS, and the transmission takes seconds.
  // So after the last queued send, stay awake for a quiet period before the radio may go to sleep.
  if (!sleeping && hasPendingWork()) {
    busy_armed = true;
    busy_until = futureMillis(TX_QUIET_MS);
  }
  bool radio_quiet = !busy_armed || millisHasNowPassed(busy_until);

  // sleep (CLI: "sleep <mins>", "sleep until", or the sleep cycle): radio fully off, no RX/TX
  if (sleep_pending && millisHasNowPassed(sleep_start_at) && !hasPendingWork() && radio_quiet
      && ((chan_q_n == 0 && bot_q_n == 0) || millisHasNowPassed(sleep_deadline))) {
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
    } else {   // woke from "sleep <minutes>" or "sleep until"
      unsigned mv = board.getBattMilliVolts();
      char msg[CHAN_MSG_MAX + 1];
      snprintf(msg, sizeof(msg), "Awake from sleep, battery %u.%02uV", mv / 1000, (mv % 1000) / 10);
      chanEnqueue(msg, CHAN_GAP_MS);
      chan_gap_on = true;
      chan_next_send_at = futureMillis(getRNG()->nextInt(2000, 20001));   // spread out nodes that wake together
    }
  }
  // end of an awake window: back to sleep (an admin login stops the cycle before this)
  if (cycle_active && !sleeping && !sleep_pending && millisHasNowPassed(cycle_awake_until) && !hasPendingWork() && radio_quiet
      && bot_q_n == 0) {
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
