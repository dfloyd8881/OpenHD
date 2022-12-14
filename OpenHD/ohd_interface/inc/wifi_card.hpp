#ifndef OPENHD_WIFI_H
#define OPENHD_WIFI_H

#include <fstream>
#include "openhd-platform.hpp"
#include <string>

#include "include_json.hpp"
#include "openhd-settings.hpp"
#include "openhd-settings2.hpp"
#include "openhd-util-filesystem.hpp"
#include "openhd-util.hpp"
#include "validate_settings_helper.h"

// R.n (20.08) this class can be summarized as following:
// 1) WifiCard: Capabilities of a detected wifi card, no persistent settings
// 2) WifiCardSettings: What to use the wifi card for, !! no frequency settings or similar. !! (note that freq and more are figured out /stored
// by the different interface types that use wifi, e.g. WBStreams or WifiHotspot

enum class WiFiCardType {
  Unknown = 0,
  Realtek8812au,
  Realtek8814au,
  Realtek88x2bu,
  Realtek8188eu,
  Atheros9khtc,
  Atheros9k,
  Ralink,
  Intel,
  Broadcom,
};
NLOHMANN_JSON_SERIALIZE_ENUM( WiFiCardType, {
   {WiFiCardType::Unknown, nullptr},
   {WiFiCardType::Realtek8812au, "Realtek8812au"},
   {WiFiCardType::Realtek8814au, "Realtek8814au"},
   {WiFiCardType::Realtek88x2bu, "Realtek88x2bu"},
   {WiFiCardType::Realtek8188eu, "Realtek8188eu"},
   {WiFiCardType::Atheros9khtc, "Atheros9khtc"},
   {WiFiCardType::Atheros9k, "Atheros9k"},
   {WiFiCardType::Ralink, "Ralink"},
   {WiFiCardType::Intel, "Intel"},
   {WiFiCardType::Broadcom, "Broadcom"},
});

static std::string wifi_card_type_to_string(const WiFiCardType &card_type) {
  switch (card_type) {
	case WiFiCardType::Realtek8812au:return "Realtek8812au";
	case WiFiCardType::Realtek8814au:return  "Realtek8814au";
	case WiFiCardType::Realtek88x2bu:return  "Realtek88x2bu";
	case WiFiCardType::Realtek8188eu:return  "Realtek8188eu";
	case WiFiCardType::Atheros9khtc:return  "Atheros9khtc";
	case WiFiCardType::Atheros9k:return  "Atheros9k";
	case WiFiCardType::Ralink:return  "Ralink";
	case WiFiCardType::Intel:return  "Intel";
	case WiFiCardType::Broadcom:return  "Broadcom";
	default: return "unknown";
  }
}


// What to use a discovered wifi card for. R.n We support hotspot or monitor mode (wifibroadcast),
// I doubt that will change.
enum class WifiUseFor {
  Unknown = 0, // Not sure what to use this wifi card for, aka unused.
  MonitorMode, //Use for wifibroadcast, aka set to monitor mode.
  Hotspot, //Use for hotspot, aka start a wifi hotspot with it.
};
NLOHMANN_JSON_SERIALIZE_ENUM( WifiUseFor, {
   {WifiUseFor::Unknown, nullptr},
   {WifiUseFor::MonitorMode, "MonitorMode"},
   {WifiUseFor::Hotspot, "Hotspot"},
});
static std::string wifi_use_for_to_string(const WifiUseFor wifi_use_for){
  switch (wifi_use_for) {
    case WifiUseFor::Hotspot:return "hotspot";
    case WifiUseFor::MonitorMode:return "monitor_mode";
    case WifiUseFor::Unknown:
    default:
      return "unknown";
  }
}

struct WiFiCard {
  // These 3 are all (slightly different) identifiers of a card on linux.
  std::string device_name;
  std::string mac;
  // phy0, phy1, needed for iw commands that don't take the device name
  int phy80211_index =-1;
  // Name of the driver that runs this card.
  std::string driver_name;
  // Detected wifi card type, generated by checking known drivers.
  WiFiCardType type = WiFiCardType::Unknown;
  bool xx_supports_5ghz = false;
  bool xx_supports_2ghz = false;
  bool supports_injection = false;
  bool supports_hotspot = false;
  bool supports_rts = false;
  bool supports_monitor_mode=false;
  bool supports_2GHz()const{
    return xx_supports_2ghz;
  };
  bool supports_5GHz()const{
    return xx_supports_5ghz;
  };
  std::vector<openhd::WifiChannel> supported_channels{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(WiFiCard,driver_name,type, device_name,mac,xx_supports_5ghz,xx_supports_2ghz,
                                   supports_injection,supports_hotspot,supports_rts)

// Only Atheros AR9271 doesn't support setting the mcs index
static bool wifi_card_supports_variable_mcs(const WiFiCard& wifi_card){
  if(wifi_card.type==WiFiCardType::Atheros9khtc || wifi_card.type==WiFiCardType::Atheros9k){
    return false;
  }
  return true;
}

// Only RTL8812au so far supports a 40Mhz channel width (and there it is also discouraged to use it)
static bool wifi_card_supports_40Mhz_channel_width(const WiFiCard& wifi_card){
  if(wifi_card.type==WiFiCardType::Realtek8812au)return true;
  return false;
}

static bool wifi_card_supports_extra_channels_2G(const WiFiCard& wi_fi_card){
  if(wi_fi_card.type==WiFiCardType::Atheros9khtc || wi_fi_card.type==WiFiCardType::Atheros9k){
    return true;
  }
  return false;
}


static bool wifi_card_supports_frequency(const OHDPlatform& platform,const WiFiCard& wifi_card,const uint32_t frequency){
  const auto channel_opt=openhd::channel_from_frequency(frequency);
  if(!channel_opt.has_value()){
    openhd::log::get_default()->debug("OpenHD doesn't know frequency {}",frequency);
    return false;
  }
  const auto& channel=channel_opt.value();
  // check if we are running on a modified kernel, in which case we can do those extra frequencies that
  // are illegal in most countries (otherwise they are disabled)
  // NOTE: When running on RPI or Jetson we assume we are running on an OpenHD image which has the modified kernel
  const bool kernel_supports_extra_channels=platform.platform_type==PlatformType::RaspberryPi ||
                                              platform.platform_type==PlatformType::Jetson;
  // check if the card generically supports the 2G or 5G space
  if(channel.space==openhd::Space::G2_4){
    if(!wifi_card.supports_2GHz()){
      return false;
    }
    if(!channel.is_standard){
      // special and only AR9271: channels below and above standard wifi
      const bool include_extra_channels_2G=kernel_supports_extra_channels && wifi_card_supports_extra_channels_2G(wifi_card);
      if(!include_extra_channels_2G){
        return false;
      }
    }
  }else{
    assert(channel.space==openhd::Space::G5_8);
    if(!wifi_card.supports_5GHz()){
      return false;
    }
    if(!channel.is_standard){
      return false;
    }
  }
  return true;
}

static std::string debug_cards(const std::vector<WiFiCard>& cards){
  std::stringstream ss;
  ss<<"size:"<<cards.size()<<"{";
  for(const auto& card:cards){
    ss<<card.device_name <<",";
  }
  ss<<"}";
  return ss.str();
}

static nlohmann::json wificards_to_json(const std::vector<WiFiCard> &cards) {
  nlohmann::json j;
  for (auto &_card: cards) {
	nlohmann::json cardJson = _card;
	j.push_back(cardJson);
  }
  return j;
}

static constexpr auto WIFI_MANIFEST_FILENAME = "/tmp/wifi_manifest";

static void write_wificards_manifest(const std::vector<WiFiCard> &cards) {
  auto manifest = wificards_to_json(cards);
  std::ofstream _t(WIFI_MANIFEST_FILENAME);
  _t << manifest.dump(4);
  _t.close();
}


#endif
