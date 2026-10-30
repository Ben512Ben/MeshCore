#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include <RTClib.h>
#include <target.h>

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  using File = fs::File;
#endif

#ifdef WITH_RS232_BRIDGE
#include "helpers/bridges/RS232Bridge.h"
#define WITH_BRIDGE
#endif

#ifdef WITH_ESPNOW_BRIDGE
#include "helpers/bridges/ESPNowBridge.h"
#define WITH_BRIDGE
#endif

#include <helpers/AdvertDataHelpers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include "RateLimiter.h"

#ifdef WITH_BRIDGE
extern AbstractBridge* bridge;
#endif

struct RepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  int16_t  noise_floor;
  int16_t  last_rssi;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct;
  uint32_t n_recv_flood, n_recv_direct;
  uint16_t err_events;                // was 'n_full_events'
  int16_t  last_snr;   // x 4
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs;
  uint32_t n_recv_errors;
};

#ifndef MAX_CLIENTS
  #define MAX_CLIENTS           32
#endif

struct NeighbourInfo {
  mesh::Identity id;
  uint32_t advert_timestamp;
  uint32_t heard_timestamp;
  int8_t snr; // multiplied by 4, user should divide to get float value
};

#ifndef FIRMWARE_BUILD_DATE
  #define FIRMWARE_BUILD_DATE   "14 Aug 2026"
#endif

#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION   "v1.17.1"
#endif

#define FIRMWARE_ROLE "repeater"

#define MAX_BLOCKHOP 20   // max repeater IDs in the 'blockhop' list
#define BLOCKHOP_PER_PAGE 5   // entries shown by one 'blockhop list'
#define CHAN_KEY_FILE "/chan_key"
#define CHAN_Q_MAX   6    // channel messages waiting to be posted
#define CHAN_MSG_MAX 150  // max bytes of a channel message ("name: text")
#define MAX_BLOCKNAME 20        // names in the 'blockname' list
#define BLOCKNAME_LEN 32        // 31 characters + terminator
#define BLOCKNAME_PER_PAGE 5    // most entries shown by one 'blockname list'
#define MAX_FILTER_KEYS 4       // extra read-only channel keys ('filterchan')
#define RON_SNAPS 25            // radio-on snapshots kept: boot + one per hourly log moment (25 = exactly 24 h span)

#define PACKET_LOG_FILE  "/packet_log"

class MyMesh : public mesh::Mesh, public CommonCLICallbacks {
  FILESYSTEM* _fs;
  uint32_t last_millis;
  uint64_t uptime_millis;
  unsigned long next_local_advert, next_flood_advert;
  bool _logging;
  NodePrefs _prefs;
  ClientACL  acl;
  CommonCLI _cli;
  uint8_t reply_data[MAX_PACKET_PAYLOAD];
  uint8_t reply_path[MAX_PATH_SIZE];
  uint8_t reply_path_len;
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;
  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;
  TransportKey default_scope;
  RateLimiter discover_limiter, anon_limiter;
  uint32_t pending_discover_tag;
  unsigned long pending_discover_until;
  bool region_load_active;
  unsigned long dirty_contacts_expiry;
#if MAX_NEIGHBOURS
  NeighbourInfo neighbours[MAX_NEIGHBOURS];
#endif
  CayenneLPP telemetry;
  unsigned long set_radio_at, revert_radio_at;
  bool sleep_pending, sleeping;
  unsigned long sleep_start_at, sleep_wake_at;
  uint32_t sleep_duration_ms;

  // sleep cycle + battery monitor (+ hourly battery log)
  bool cycle_active, sleep_auto_enabled;
  unsigned long cycle_awake_until;
  bool batt_check_armed;
  unsigned long next_batt_check;
  uint8_t batt_low_count, batt_crit_count;
  uint32_t batt_check_n;
  uint16_t batt_log[24];
  int16_t temp_log[24];   // chip temperature, tenths of a degree C (INT16_MIN = no reading)
  uint8_t batt_log_head, batt_log_count;
  uint16_t batt_prev24;        // the reading the newest one replaced = exactly 24 h older (item 33)
  bool batt_trend_valid;       // true once 25 hourly readings exist
  uint32_t radio_on_total_ms;  // radio-on time added up (wraps; only differences are used) (item 34)
  uint32_t radio_on_since_ms;  // millis() when the radio last came on
  uint32_t ron_snap_t[RON_SNAPS];    // millis() at boot and at every hourly log moment
  uint32_t ron_snap_on[RON_SNAPS];   // radio-on total at those moments
  uint8_t ron_snap_head, ron_snap_n;

  // 'blockhop' list (RAM only): repeaters whose relayed packets (first hop) we refuse to forward
  struct BlockHopEntry {
    uint8_t key[3];          // first bytes of the repeater's public key (= its path hash)
    uint8_t len;             // 1..3 valid bytes in key, 0 = unused slot
    bool is_auto;            // added automatically from an advert
    uint32_t hits;           // packets blocked
    unsigned long last_hit_ms;
    unsigned long last_activity_ms;
  };
  BlockHopEntry blockhop[MAX_BLOCKHOP];

  // 'blockname' list (RAM only): senders on the Public / filter channels whose messages we refuse to relay
  struct BlockNameEntry {
    char name[BLOCKNAME_LEN];   // as typed, "" = unused slot. A trailing '*' matches any name starting with the text before it
    uint32_t hits;              // messages blocked
    unsigned long last_hit_ms;
  };
  BlockNameEntry blockname[MAX_BLOCKNAME];
  bool block_name_hit;          // set while ONE packet is processed: its sender is on the blockname list (cleared in onRecvPacket)
  mesh::GroupChannel public_chan;   // built-in Public channel key. Read-only: name check only, never commands
  struct FilterKey {
    bool set;
    uint8_t len;
    mesh::GroupChannel ch;
  };
  FilterKey filter_keys[MAX_FILTER_KEYS];   // extra read-only channel keys (RAM only)

  // 'chanallow' relay whitelist (RAM only, off after every boot)
  bool chanallow_on, chanallow_extra_set;
  uint8_t chanallow_extra_hash;
  uint32_t chanallow_dropped;       // channel messages not relayed
  uint32_t chanallow_adv_dropped;   // flood adverts not relayed (item 36)

  int16_t cmd_hops;             // hops the channel command being run took to reach us (-1 = not from the channel)
  bool sleep_cmd_flag;          // set by startSleep(): the command being run started a sleep
  uint8_t list_pages_pending;   // 'blockname list' over the channel: total pages to send (0 = just the one reply) (item 38)

  // warnings / notices
  uint8_t hot_count;
  bool hot_msg_sent, crit_msg_sent;
  uint8_t blk_pending[MAX_BLOCKHOP][3];   // newly auto-blocked IDs not yet announced
  uint8_t blk_pending_len[MAX_BLOCKHOP];
  uint8_t blk_pending_n;
  bool blk_msg_armed;
  unsigned long blk_next_msg;
  unsigned long sleep_deadline;           // sleep starts at the latest here, even if channel messages are still queued

  // channel posting (key is kept in RAM only, lost on reboot)
  bool chan_key_set;
  uint8_t chan_key_len;
  mesh::GroupChannel chan;
  struct ChanQueued {
    char text[CHAN_MSG_MAX + 1];
    uint32_t gap_after_ms;
    bool is_report;
  };
  ChanQueued chan_q[CHAN_Q_MAX];
  uint8_t chan_q_n;
  bool chan_gap_on;
  unsigned long chan_next_send_at;
  bool report_armed;
  unsigned long report_due_at;
  uint32_t last_cmd_ts;   // newest channel command accepted (older/equal ones are ignored: replay protection)
  bool from_channel;      // true while a command received on the channel is being run
  unsigned long sleep_target_at;   // 'sleep until': the wake time, counted from the command
  bool sleep_target_abs;
  bool busy_armed;                 // sending seen recently ...
  unsigned long busy_until;        // ... radio stays on until a quiet period has passed
  bool boot_notice_pending, boot_notice_armed;   // 'Rebooted' channel message after start-up
  unsigned long boot_notice_at;
  // watchdog / radio health (item 43)
  bool wdt_feed_ok;               // false = main loop stops feeding the hardware watchdog, so it reboots the node
  uint32_t wd_last_tick;          // millis() at the previous watchdogService() pass
  uint32_t wd_last_sent;          // radio sent counter at the previous pass
  uint32_t wd_tx_stuck_ms;        // awake ms with packets queued and nothing sent
  bool wd_tx_restarted;           // radio already restarted for this stuck spell
  uint32_t wd_last_rx;            // radio received counter (good + bad packets) at the previous pass
  uint32_t wd_silence_ms;         // awake ms with nothing heard
  bool wd_silence_msg_sent;       // 'No packets heard' already posted for this silent spell
  bool wd_grace_over;             // node has been up long enough for a radio check to cause a reboot
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;
  int  matching_peer_indexes[MAX_CLIENTS];
#if defined(WITH_RS232_BRIDGE)
  RS232Bridge bridge;
#elif defined(WITH_ESPNOW_BRIDGE)
  ESPNowBridge bridge;
#endif

  void putNeighbour(const mesh::Identity& id, uint32_t timestamp, float snr);
  uint8_t handleLoginReq(const mesh::Identity& sender, const uint8_t* secret, uint32_t sender_timestamp, const uint8_t* data, bool is_flood);
  bool handleSleepCommands(uint32_t sender_timestamp, char* command, char* reply);
  bool handleSleepCommandsInner(uint32_t sender_timestamp, char* command, char* reply);
  void startSleep(uint32_t duration_ms, bool absolute = false);
  void radioSleep(uint32_t duration_ms);
  void radioWake();
  void stopSleepCycle();
  void batteryMonitor();
  void watchdogService();
  void radioRestart();
  void formatBattLog(char* reply);
  void formatBattPct(char* reply);
  void formatBattTrend(char* reply);
  uint32_t radioOnMs() const;
  void ronSnapshot();
  int radioOnPercentSinceOldest() const;
  int radioOnPercentTrend() const;
  void formatTempLog(char* reply);
  bool blockHopCheck(const mesh::Packet* packet);
  bool blockHopAdd(const uint8_t* key, uint8_t len, bool is_auto);
  void formatBlockHopList(char* reply, int page);
  uint8_t blockNameCount() const;
  void blockNameCheckText(const char* text);
  int formatBlockNameList(char* reply, int page);   // returns the number of pages
  bool chanAllowBlocks(const mesh::Packet* packet);
  bool setChannelKey(const char* key);
  void saveChannelKey();
  void loadChannelKey();
  void queueReport();
  void formatStatus(char* reply);
  bool postChannel(const char* text);
  void chanEnqueue(const char* text, uint32_t gap_after_ms, bool is_report = false);
  void chanDropReports();
  void chanService();
  void dailyReport();
  void noteAutoBlock(const uint8_t* key, uint8_t len);
  void blockNoticeService();
  void handleChannelCommand(const char* text, uint32_t ts, int hops);
  uint8_t handleAnonRegionsReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonOwnerReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  uint8_t handleAnonClockReq(const mesh::Identity& sender, uint32_t sender_timestamp, const uint8_t* data);
  int handleRequest(ClientInfo* sender, uint32_t sender_timestamp, uint8_t* payload, size_t payload_len);
  mesh::Packet* createSelfAdvert();

  File openAppend(const char* fname);
  bool isLooped(const mesh::Packet* packet, const uint8_t max_counters[]);

protected:
  float getAirtimeBudgetFactor() const override {
    return _prefs.airtime_factor;
  }

  bool allowPacketForward(const mesh::Packet* packet) override;
  const char* getLogDateTime() override;
  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;

  void logRx(mesh::Packet* pkt, int len, float score) override;
  void logTx(mesh::Packet* pkt, int len) override;
  void logTxFail(mesh::Packet* pkt, int len) override;
  int calcRxDelay(float score, uint32_t air_time) const override;

  uint32_t getRetransmitDelay(const mesh::Packet* packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override;

  int getInterferenceThreshold() const override {
    return _prefs.interference_threshold;
  }
  bool getCADEnabled() const override {
    return _prefs.cad_enabled;
  }
  int getAGCResetInterval() const override {
    return ((int)_prefs.agc_reset_interval) * 4000;   // milliseconds
  }
  uint8_t getExtraAckTransmitCount() const override {
    return _prefs.multi_acks;
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled?"1":"0");
  }
#endif

  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;

  void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret, const mesh::Identity& sender, uint8_t* data, size_t len) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len);
  void onPeerDataRecv(mesh::Packet* packet, uint8_t type, int sender_idx, const uint8_t* secret, uint8_t* data, size_t len) override;
  int searchChannelsByHash(const uint8_t* hash, mesh::GroupChannel channels[], int max_matches) override;
  void onGroupDataRecv(mesh::Packet* packet, uint8_t type, const mesh::GroupChannel& channel, uint8_t* data, size_t len) override;
  bool onPeerPathRecv(mesh::Packet* packet, int sender_idx, const uint8_t* secret, uint8_t* path, uint8_t path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onControlDataRecv(mesh::Packet* packet) override;

  void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);

public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables);

  void begin(FILESYSTEM* fs);
  void sendNodeDiscoverReq();
  const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
  const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
  const char* getRole() override { return FIRMWARE_ROLE; }
  const char* getNodeName() { return _prefs.node_name; }
  NodePrefs* getNodePrefs() {
    return &_prefs;
  }

  void savePrefs() override {
    _cli.savePrefs(_fs);
  }

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);

  // CommonCLICallbacks
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;
  bool sleepFor(uint32_t duration_ms) override;
  bool formatFileSystem() override;
  void sendSelfAdvertisement(int delay_millis, bool flood) override;
  void updateAdvertTimer() override;
  void updateFloodAdvertTimer() override;

  void setLoggingOn(bool enable) override { _logging = enable; }

  void eraseLogFile() override {
    _fs->remove(PACKET_LOG_FILE);
  }

  void dumpLogFile() override;
  void setTxPower(int8_t power_dbm) override;
  void formatNeighborsReply(char *reply) override;
  void removeNeighbor(const uint8_t* pubkey, int key_len) override;
  void formatStatsReply(char *reply) override;
  void formatRadioStatsReply(char *reply) override;
  void formatPacketStatsReply(char *reply) override;
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

  mesh::LocalIdentity& getSelfId() override { return self_id; }

  void saveIdentity(const mesh::LocalIdentity& new_id) override;
  void clearStats() override;

  void handleCommand(uint32_t sender_timestamp, char* command, char* reply);
  void loop();

#if defined(WITH_BRIDGE)
  void setBridgeState(bool enable) override {
    if (enable == bridge.isRunning()) return;
    if (enable)
    {
      bridge.begin();
    }
    else 
    {
      bridge.end();
    }
  }

  void restartBridge() override {
    if (!bridge.isRunning()) return;
    bridge.end();
    bridge.begin();
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  // true while timed channel work is waiting (ESP32 only): the main loop must not light-sleep for up to 30 s then
  bool needsFastLoop() const {
#if defined(ESP32)
    return sleep_pending || (chan_q_n > 0 && !sleeping);
#else
    return false;
#endif
  }

  // false once a radio check has decided the node must be rebooted: main.cpp then stops feeding the hardware watchdog
  bool watchdogFeedOk() const { return wdt_feed_ok; }

  bool setRxBoostedGain(bool enable) override;

  #if defined(USE_LR2021)
  virtual bool configSideDetectors(const uint8_t sideDetSFs[], uint8_t num, float bw) override;
  #endif

};
