/*
 * EMS-ESP - https://github.com/emsesp/EMS-ESP
 * Copyright 2020  Paul Derbyshire
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "boiler.h"

namespace emsesp {

REGISTER_FACTORY(Boiler, EMSdevice::DeviceType::BOILER)

uuid::log::Logger Boiler::logger_{F_(boiler), uuid::log::Facility::CONSOLE};

Boiler::Boiler(uint8_t device_type, int8_t device_id, uint8_t product_id, const char * version, const std::string & name, uint8_t flags, uint8_t brand)
    : EMSdevice(device_type, device_id, product_id, version, name, flags, brand) {
    // cascaded heatingsources, only some values per individual heatsource (hs)
    if (device_id != EMSdevice::EMS_DEVICE_ID_BOILER) {
        uint8_t hs = device_id - EMSdevice::EMS_DEVICE_ID_BOILER_1; // heating source id, count from 0
        // Runtime of each heatingsource in 0x06DC, ff
        register_telegram_type(0x6DC + hs, F("CascadeMessage"), false, MAKE_PF_CB(process_CascadeMessage));
        register_device_value(DeviceValueTAG::TAG_HS1 + hs, &burnWorkMin_, DeviceValueType::TIME, nullptr, FL_(burnWorkMin), DeviceValueUOM::MINUTES);
        // selBurnpower in D2 and E4
        // register_telegram_type(0xD2, F("CascadePowerMessage"), false, MAKE_PF_CB(process_CascadePowerMessage));
        // individual Flowtemps and powervalues for each heatingsource in E4
        register_telegram_type(0xE4, F("UBAMonitorFastPlus"), false, MAKE_PF_CB(process_UBAMonitorFastPlus));
        register_device_value(DeviceValueTAG::TAG_HS1 + hs, &selFlowTemp_, DeviceValueType::UINT, nullptr, FL_(selFlowTemp), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_HS1 + hs, &selBurnPow_, DeviceValueType::UINT, nullptr, FL_(selBurnPow), DeviceValueUOM::PERCENT);
        register_device_value(DeviceValueTAG::TAG_HS1 + hs, &curFlowTemp_, DeviceValueType::USHORT, FL_(div10), FL_(curFlowTemp), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_HS1 + hs, &curBurnPow_, DeviceValueType::UINT, nullptr, FL_(curBurnPow), DeviceValueUOM::PERCENT);
        return;
    }

    // register values for master boiler/cascade module
    // reserve_telegram_functions(25); // reserve some space for the telegram registries, to avoid memory fragmentation

    // the telegram handlers...
    // common for all boilers
    register_telegram_type(0x10, F("UBAErrorMessage1"), false, MAKE_PF_CB(process_UBAErrorMessage));
    register_telegram_type(0x11, F("UBAErrorMessage2"), false, MAKE_PF_CB(process_UBAErrorMessage));
    register_telegram_type(0xC2, F("UBAErrorMessage3"), false, MAKE_PF_CB(process_UBAErrorMessage2));
    register_telegram_type(0x14, F("UBATotalUptime"), true, MAKE_PF_CB(process_UBATotalUptime));
    register_telegram_type(0x15, F("UBAMaintenanceData"), false, MAKE_PF_CB(process_UBAMaintenanceData));
    register_telegram_type(0x1C, F("UBAMaintenanceStatus"), false, MAKE_PF_CB(process_UBAMaintenanceStatus));
    // EMS1.0 and maybe EMS+?
    register_telegram_type(0x18, F("UBAMonitorFast"), false, MAKE_PF_CB(process_UBAMonitorFast));
    register_telegram_type(0x19, F("UBAMonitorSlow"), false, MAKE_PF_CB(process_UBAMonitorSlow));
    register_telegram_type(0x1A, F("UBASetPoints"), false, MAKE_PF_CB(process_UBASetPoints));
    register_telegram_type(0x35, F("UBAFlags"), false, MAKE_PF_CB(process_UBAFlags));
    // only EMS 1.0
    register_telegram_type(0x16, F("UBAParameters"), true, MAKE_PF_CB(process_UBAParameters));
    register_telegram_type(0x33, F("UBAParameterWW"), true, MAKE_PF_CB(process_UBAParameterWW));
    register_telegram_type(0x34, F("UBAMonitorWW"), false, MAKE_PF_CB(process_UBAMonitorWW));
    // not ems1.0, but HT3
    if (model() != EMSdevice::EMS_DEVICE_FLAG_EMS) {
        register_telegram_type(0x26, F("UBASettingsWW"), true, MAKE_PF_CB(process_UBASettingsWW));
        register_telegram_type(0x2A, F("MC110Status"), false, MAKE_PF_CB(process_MC110Status));
    }

    // only EMS+
    if (model() != EMSdevice::EMS_DEVICE_FLAG_EMS && model() != EMSdevice::EMS_DEVICE_FLAG_HT3) {
        register_telegram_type(0xD1, F("UBAOutdoorTemp"), false, MAKE_PF_CB(process_UBAOutdoorTemp));
        register_telegram_type(0xE3, F("UBAMonitorSlowPlus2"), false, MAKE_PF_CB(process_UBAMonitorSlowPlus2));
        register_telegram_type(0xE4, F("UBAMonitorFastPlus"), false, MAKE_PF_CB(process_UBAMonitorFastPlus));
        register_telegram_type(0xE5, F("UBAMonitorSlowPlus"), false, MAKE_PF_CB(process_UBAMonitorSlowPlus));
        register_telegram_type(0xE6, F("UBAParametersPlus"), true, MAKE_PF_CB(process_UBAParametersPlus));
        register_telegram_type(0xE9, F("UBAMonitorWWPlus"), false, MAKE_PF_CB(process_UBAMonitorWWPlus));
        register_telegram_type(0xEA, F("UBAParameterWWPlus"), true, MAKE_PF_CB(process_UBAParameterWWPlus));
    }

    if (model() == EMSdevice::EMS_DEVICE_FLAG_HEATPUMP) {
        register_telegram_type(0x494, F("UBAEnergySupplied"), false, MAKE_PF_CB(process_UBAEnergySupplied));
        register_telegram_type(0x495, F("UBAInformation"), false, MAKE_PF_CB(process_UBAInformation));
        register_telegram_type(0x48D, F("HpPower"), true, MAKE_PF_CB(process_HpPower));
        register_telegram_type(0x48F, F("HpOutdoor"), false, MAKE_PF_CB(process_HpOutdoor));
        register_telegram_type(0x48A, F("HpPool"), true, MAKE_PF_CB(process_HpPool));
    }

    // reset is a command uses a dummy variable which is always zero, shown as blank, but provides command enum options
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &reset_, DeviceValueType::CMD, FL_(enum_reset), FL_(reset), DeviceValueUOM::NONE, MAKE_CF_CB(set_reset));
    has_update(reset_, 0);

    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &heatingActive_, DeviceValueType::BOOL, nullptr, FL_(heatingActive), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &tapwaterActive_, DeviceValueType::BOOL, nullptr, FL_(tapwaterActive), DeviceValueUOM::NONE);
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &selFlowTemp_, DeviceValueType::UINT, nullptr, FL_(selFlowTemp), DeviceValueUOM::DEGREES, MAKE_CF_CB(set_flow_temp));
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &selBurnPow_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(selBurnPow),
                          DeviceValueUOM::PERCENT,
                          MAKE_CF_CB(set_burn_power),
                          0,
                          130);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &heatingPumpMod_, DeviceValueType::UINT, nullptr, FL_(heatingPumpMod), DeviceValueUOM::PERCENT);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &heatingPump2Mod_, DeviceValueType::UINT, nullptr, FL_(heatingPump2Mod), DeviceValueUOM::PERCENT);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &outdoorTemp_, DeviceValueType::SHORT, FL_(div10), FL_(outdoorTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &curFlowTemp_, DeviceValueType::USHORT, FL_(div10), FL_(curFlowTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &retTemp_, DeviceValueType::USHORT, FL_(div10), FL_(retTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &switchTemp_, DeviceValueType::USHORT, FL_(div10), FL_(switchTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &sysPress_, DeviceValueType::UINT, FL_(div10), FL_(sysPress), DeviceValueUOM::BAR);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &boilTemp_, DeviceValueType::USHORT, FL_(div10), FL_(boilTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &exhaustTemp_, DeviceValueType::USHORT, FL_(div10), FL_(exhaustTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &burnGas_, DeviceValueType::BOOL, nullptr, FL_(burnGas), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &burnGas2_, DeviceValueType::BOOL, nullptr, FL_(burnGas2), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &flameCurr_, DeviceValueType::USHORT, FL_(div10), FL_(flameCurr), DeviceValueUOM::UA);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &heatingPump_, DeviceValueType::BOOL, nullptr, FL_(heatingPump), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &fanWork_, DeviceValueType::BOOL, nullptr, FL_(fanWork), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &ignWork_, DeviceValueType::BOOL, nullptr, FL_(ignWork), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &oilPreHeat_, DeviceValueType::BOOL, nullptr, FL_(oilPreHeat), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &heatingActivated_,
                          DeviceValueType::BOOL,
                          nullptr,
                          FL_(heatingActivated),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_heating_activated));
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &heatingTemp_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(heatingTemp),
                          DeviceValueUOM::DEGREES,
                          MAKE_CF_CB(set_heating_temp));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &pumpModMax_, DeviceValueType::UINT, nullptr, FL_(pumpModMax), DeviceValueUOM::PERCENT, MAKE_CF_CB(set_max_pump));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &pumpModMin_, DeviceValueType::UINT, nullptr, FL_(pumpModMin), DeviceValueUOM::PERCENT, MAKE_CF_CB(set_min_pump));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &pumpDelay_, DeviceValueType::UINT, nullptr, FL_(pumpDelay), DeviceValueUOM::MINUTES, MAKE_CF_CB(set_pump_delay));
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &burnMinPeriod_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(burnMinPeriod),
                          DeviceValueUOM::MINUTES,
                          MAKE_CF_CB(set_burn_period));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &burnMinPower_, DeviceValueType::UINT, nullptr, FL_(burnMinPower), DeviceValueUOM::PERCENT, MAKE_CF_CB(set_min_power));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &burnMaxPower_, DeviceValueType::UINT, nullptr, FL_(burnMaxPower), DeviceValueUOM::PERCENT, MAKE_CF_CB(set_max_power));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &boilHystOn_, DeviceValueType::INT, nullptr, FL_(boilHystOn), DeviceValueUOM::DEGREES_R, MAKE_CF_CB(set_hyst_on));
    register_device_value(
        DeviceValueTAG::TAG_BOILER_DATA, &boilHystOff_, DeviceValueType::INT, nullptr, FL_(boilHystOff), DeviceValueUOM::DEGREES_R, MAKE_CF_CB(set_hyst_off));
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &setFlowTemp_, DeviceValueType::UINT, nullptr, FL_(setFlowTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &setBurnPow_, DeviceValueType::UINT, nullptr, FL_(setBurnPow), DeviceValueUOM::PERCENT);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &curBurnPow_, DeviceValueType::UINT, nullptr, FL_(curBurnPow), DeviceValueUOM::PERCENT);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &burnStarts_, DeviceValueType::ULONG, nullptr, FL_(burnStarts), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &burnWorkMin_, DeviceValueType::TIME, nullptr, FL_(burnWorkMin), DeviceValueUOM::MINUTES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &heatWorkMin_, DeviceValueType::TIME, nullptr, FL_(heatWorkMin), DeviceValueUOM::MINUTES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &UBAuptime_, DeviceValueType::TIME, nullptr, FL_(UBAuptime), DeviceValueUOM::MINUTES);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &lastCode_, DeviceValueType::STRING, nullptr, FL_(lastCode), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &serviceCode_, DeviceValueType::STRING, nullptr, FL_(serviceCode), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &serviceCodeNumber_, DeviceValueType::USHORT, nullptr, FL_(serviceCodeNumber), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &maintenanceMessage_, DeviceValueType::STRING, nullptr, FL_(maintenanceMessage), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &maintenanceType_,
                          DeviceValueType::ENUM,
                          FL_(enum_off_time_date_manual),
                          FL_(maintenanceType),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_maintenance));
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &maintenanceTime_,
                          DeviceValueType::USHORT,
                          nullptr,
                          FL_(maintenanceTime),
                          DeviceValueUOM::HOURS,
                          MAKE_CF_CB(set_maintenancetime));
    register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                          &maintenanceDate_,
                          DeviceValueType::STRING,
                          FL_(tpl_date),
                          FL_(maintenanceDate),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_maintenancedate));

    // heatpump info
    if (model() == EMS_DEVICE_FLAG_HEATPUMP) {
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &upTimeControl_, DeviceValueType::TIME, FL_(div60), FL_(upTimeControl), DeviceValueUOM::MINUTES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &upTimeCompHeating_, DeviceValueType::TIME, FL_(div60), FL_(upTimeCompHeating), DeviceValueUOM::MINUTES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &upTimeCompCooling_, DeviceValueType::TIME, FL_(div60), FL_(upTimeCompCooling), DeviceValueUOM::MINUTES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &upTimeCompWw_, DeviceValueType::TIME, FL_(div60), FL_(upTimeCompWw), DeviceValueUOM::MINUTES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &upTimeCompPool_, DeviceValueType::TIME, FL_(div60), FL_(upTimeCompPool), DeviceValueUOM::MINUTES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &totalCompStarts_, DeviceValueType::ULONG, nullptr, FL_(totalCompStarts), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &heatingStarts_, DeviceValueType::ULONG, nullptr, FL_(heatingStarts), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &coolingStarts_, DeviceValueType::ULONG, nullptr, FL_(coolingStarts), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &wwStarts2_, DeviceValueType::ULONG, nullptr, FL_(wwStarts2), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &poolStarts_, DeviceValueType::ULONG, nullptr, FL_(poolStarts), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgConsTotal_, DeviceValueType::ULONG, nullptr, FL_(nrgConsTotal), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgConsCompTotal_, DeviceValueType::ULONG, nullptr, FL_(nrgConsCompTotal), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgConsCompHeating_, DeviceValueType::ULONG, nullptr, FL_(nrgConsCompHeating), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgConsCompWw_, DeviceValueType::ULONG, nullptr, FL_(nrgConsCompWw), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgConsCompCooling_, DeviceValueType::ULONG, nullptr, FL_(nrgConsCompCooling), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgConsCompPool_, DeviceValueType::ULONG, nullptr, FL_(nrgConsCompPool), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                              &auxElecHeatNrgConsTotal_,
                              DeviceValueType::ULONG,
                              nullptr,
                              FL_(auxElecHeatNrgConsTotal),
                              DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                              &auxElecHeatNrgConsHeating_,
                              DeviceValueType::ULONG,
                              nullptr,
                              FL_(auxElecHeatNrgConsHeating),
                              DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                              &auxElecHeatNrgConsWW_,
                              DeviceValueType::ULONG,
                              nullptr,
                              FL_(auxElecHeatNrgConsWW),
                              DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                              &auxElecHeatNrgConsPool_,
                              DeviceValueType::ULONG,
                              nullptr,
                              FL_(auxElecHeatNrgConsPool),
                              DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgSuppTotal_, DeviceValueType::ULONG, nullptr, FL_(nrgSuppTotal), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgSuppHeating_, DeviceValueType::ULONG, nullptr, FL_(nrgSuppHeating), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgSuppWw_, DeviceValueType::ULONG, nullptr, FL_(nrgSuppWw), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgSuppCooling_, DeviceValueType::ULONG, nullptr, FL_(nrgSuppCooling), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &nrgSuppPool_, DeviceValueType::ULONG, nullptr, FL_(nrgSuppPool), DeviceValueUOM::KWH);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpPower_, DeviceValueType::UINT, FL_(div10), FL_(hpPower), DeviceValueUOM::KW);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpCompOn_, DeviceValueType::BOOL, nullptr, FL_(hpCompOn), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpActivity_, DeviceValueType::ENUM, FL_(enum_hpactivity), FL_(hpActivity), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpHeatingOn_, DeviceValueType::BOOL, nullptr, FL_(hpHeatingOn), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpCoolingOn_, DeviceValueType::BOOL, nullptr, FL_(hpCoolingOn), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpWwOn_, DeviceValueType::BOOL, nullptr, FL_(hpWwOn), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpPoolOn_, DeviceValueType::BOOL, nullptr, FL_(hpPoolOn), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpBrinePumpSpd_, DeviceValueType::UINT, nullptr, FL_(hpBrinePumpSpd), DeviceValueUOM::PERCENT);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpSwitchValve_, DeviceValueType::BOOL, nullptr, FL_(hpSwitchValve), DeviceValueUOM::NONE);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpCompSpd_, DeviceValueType::UINT, nullptr, FL_(hpCompSpd), DeviceValueUOM::PERCENT);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpCircSpd_, DeviceValueType::UINT, nullptr, FL_(hpCircSpd), DeviceValueUOM::PERCENT);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpBrineIn_, DeviceValueType::SHORT, FL_(div10), FL_(hpBrineIn), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpBrineOut_, DeviceValueType::SHORT, FL_(div10), FL_(hpBrineOut), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpSuctionGas_, DeviceValueType::SHORT, FL_(div10), FL_(hpSuctionGas), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpHotGas_, DeviceValueType::SHORT, FL_(div10), FL_(hpHotGas), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTc0_, DeviceValueType::SHORT, FL_(div10), FL_(hpTc0), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTc1_, DeviceValueType::SHORT, FL_(div10), FL_(hpTc1), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTc3_, DeviceValueType::SHORT, FL_(div10), FL_(hpTc3), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTr3_, DeviceValueType::SHORT, FL_(div10), FL_(hpTr3), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTr4_, DeviceValueType::SHORT, FL_(div10), FL_(hpTr4), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTr5_, DeviceValueType::SHORT, FL_(div10), FL_(hpTr5), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTr6_, DeviceValueType::SHORT, FL_(div10), FL_(hpTr6), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTr7_, DeviceValueType::SHORT, FL_(div10), FL_(hpTr7), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpTl2_, DeviceValueType::SHORT, FL_(div10), FL_(hpTl2), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpPl1_, DeviceValueType::SHORT, FL_(div10), FL_(hpPl1), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA, &hpPh1_, DeviceValueType::SHORT, FL_(div10), FL_(hpPh1), DeviceValueUOM::DEGREES);
        register_device_value(DeviceValueTAG::TAG_BOILER_DATA,
                              &poolSetTemp_,
                              DeviceValueType::UINT,
                              FL_(div2),
                              FL_(poolSetTemp),
                              DeviceValueUOM::DEGREES,
                              MAKE_CF_CB(set_pool_temp));
    }

    // dhw - boiler_data_ww topic
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwTapActivated_,
                          DeviceValueType::BOOL,
                          nullptr,
                          FL_(wwtapactivated),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_tapwarmwater_activated));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwSetTemp_, DeviceValueType::UINT, nullptr, FL_(wwSetTemp), DeviceValueUOM::DEGREES);
    register_device_value(
        DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwSelTemp_, DeviceValueType::UINT, nullptr, FL_(wwSelTemp), DeviceValueUOM::DEGREES, MAKE_CF_CB(set_ww_temp));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwSelTempLow_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(wwSelTempLow),
                          DeviceValueUOM::DEGREES,
                          MAKE_CF_CB(set_ww_temp_low));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwSelTempOff_, DeviceValueType::UINT, nullptr, FL_(wwSelTempOff), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwSelTempSingle_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(wwSelTempSingle),
                          DeviceValueUOM::DEGREES,
                          MAKE_CF_CB(set_ww_temp_single));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwType_, DeviceValueType::ENUM, FL_(enum_flow), FL_(wwType), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwComfort_,
                          DeviceValueType::ENUM,
                          FL_(enum_comfort),
                          FL_(wwComfort),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_ww_mode));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwFlowTempOffset_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(wwFlowTempOffset),
                          DeviceValueUOM::DEGREES_R,
                          MAKE_CF_CB(set_ww_flowTempOffset));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwMaxPower_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(wwMaxPower),
                          DeviceValueUOM::PERCENT,
                          MAKE_CF_CB(set_ww_maxpower));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwCircPump_,
                          DeviceValueType::BOOL,
                          nullptr,
                          FL_(wwCircPump),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_ww_circulation_pump));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwChargeType_, DeviceValueType::ENUM, FL_(enum_charge), FL_(wwChargeType), DeviceValueUOM::NONE);
    register_device_value(
        DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwHystOn_, DeviceValueType::INT, nullptr, FL_(wwHystOn), DeviceValueUOM::DEGREES_R, MAKE_CF_CB(set_ww_hyst_on));
    register_device_value(
        DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwHystOff_, DeviceValueType::INT, nullptr, FL_(wwHystOff), DeviceValueUOM::DEGREES_R, MAKE_CF_CB(set_ww_hyst_off));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwDisinfectionTemp_,
                          DeviceValueType::UINT,
                          nullptr,
                          FL_(wwDisinfectionTemp),
                          DeviceValueUOM::DEGREES,
                          MAKE_CF_CB(set_ww_disinfect_temp));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwCircMode_,
                          DeviceValueType::ENUM,
                          FL_(enum_freq),
                          FL_(wwCircMode),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_ww_circulation_mode));
    register_device_value(
        DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwCirc_, DeviceValueType::BOOL, nullptr, FL_(wwCirc), DeviceValueUOM::NONE, MAKE_CF_CB(set_ww_circulation));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwCurTemp_, DeviceValueType::USHORT, FL_(div10), FL_(wwCurTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwCurTemp2_, DeviceValueType::USHORT, FL_(div10), FL_(wwCurTemp2), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwCurFlow_, DeviceValueType::UINT, FL_(div10), FL_(wwCurFlow), DeviceValueUOM::LMIN);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwStorageTemp1_, DeviceValueType::USHORT, FL_(div10), FL_(wwStorageTemp1), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwStorageTemp2_, DeviceValueType::USHORT, FL_(div10), FL_(wwStorageTemp2), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwActivated_,
                          DeviceValueType::BOOL,
                          nullptr,
                          FL_(wwActivated),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_ww_activated));
    register_device_value(
        DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwOneTime_, DeviceValueType::BOOL, nullptr, FL_(wwOneTime), DeviceValueUOM::NONE, MAKE_CF_CB(set_ww_onetime));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW,
                          &wwDisinfect_,
                          DeviceValueType::BOOL,
                          nullptr,
                          FL_(wwDisinfecting),
                          DeviceValueUOM::NONE,
                          MAKE_CF_CB(set_ww_disinfect));
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwCharging_, DeviceValueType::BOOL, nullptr, FL_(wwCharging), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwRecharging_, DeviceValueType::BOOL, nullptr, FL_(wwRecharging), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwTempOK_, DeviceValueType::BOOL, nullptr, FL_(wwTempOK), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwActive_, DeviceValueType::BOOL, nullptr, FL_(wwActive), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &ww3wayValve_, DeviceValueType::BOOL, nullptr, FL_(ww3wayValve), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwSetPumpPower_, DeviceValueType::UINT, nullptr, FL_(wwSetPumpPower), DeviceValueUOM::PERCENT);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwMixerTemp_, DeviceValueType::USHORT, FL_(div10), FL_(wwMixerTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwCylMiddleTemp_, DeviceValueType::USHORT, FL_(div10), FL_(wwCylMiddleTemp), DeviceValueUOM::DEGREES);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwStarts_, DeviceValueType::ULONG, nullptr, FL_(wwStarts), DeviceValueUOM::NONE);
    register_device_value(DeviceValueTAG::TAG_DEVICE_DATA_WW, &wwWorkM_, DeviceValueType::TIME, nullptr, FL_(wwWorkM), DeviceValueUOM::MINUTES);

    // fetch some initial data
    EMSESP::send_read_request(0x10, device_id); // read last errorcode on start (only published on errors)
    EMSESP::send_read_request(0x11, device_id); // read last errorcode on start (only published on errors)
    EMSESP::send_read_request(0x15, device_id); // read maintenace data on start (only published on change)
    EMSESP::send_read_request(0x1C, device_id); // read maintenace status on start (only published on change)
    EMSESP::send_read_request(0xC2, device_id); // read last errorcode on start (only published on errors)
}

// Check if hot tap water or heating is active
// Values will always be posted first time as heatingActive_ and tapwaterActive_ will have values EMS_VALUE_BOOL_NOTSET
void Boiler::check_active(const bool force) {
    if (!Helpers::hasValue(boilerState_)) {
        return;
    }

    bool    b;
    uint8_t val;

    // check if heating is active, bits 2 and 4 must be set
    b   = ((boilerState_ & 0x09) == 0x09);
    val = b ? EMS_VALUE_BOOL_ON : EMS_VALUE_BOOL_OFF;
    if (heatingActive_ != val || force) {
        heatingActive_ = val;
        char s[7];
        Mqtt::publish(F_(heating_active), Helpers::render_boolean(s, b));
    }

    // check if we can use tapactivated in flow systems
    if ((wwType_ == 1) && !Helpers::hasValue(wwTapActivated_, EMS_VALUE_BOOL)) {
        wwTapActivated_ = 1;
    }

    // check if tap water is active, bits 1 and 4 must be set
    // also check if there is a flowsensor and flow-type
    static bool flowsensor = false;
    if (Helpers::hasValue(wwCurFlow_) && (wwCurFlow_ > 0) && (wwType_ == 1)) {
        flowsensor = true;
    }
    if (flowsensor) {
        b = ((wwCurFlow_ > 0) && ((boilerState_ & 0x0A) == 0x0A));
    } else {
        b = ((boilerState_ & 0x0A) == 0x0A);
    }

    val = b ? EMS_VALUE_BOOL_ON : EMS_VALUE_BOOL_OFF;
    if (tapwaterActive_ != val || force) {
        tapwaterActive_ = val;
        char s[7];
        Mqtt::publish(F_(tapwater_active), Helpers::render_boolean(s, b));
        EMSESP::tap_water_active(b); // let EMS-ESP know, used in the Shower class
    }
}

// 0x18
void Boiler::process_UBAMonitorFast(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, selFlowTemp_, 0);
    has_update(telegram, curFlowTemp_, 1);
    has_update(telegram, selBurnPow_, 3); // burn power max setting
    has_update(telegram, curBurnPow_, 4);
    has_update(telegram, boilerState_, 5);

    has_bitupdate(telegram, burnGas_, 7, 0);
    has_bitupdate(telegram, burnGas2_, 7, 1);
    has_bitupdate(telegram, fanWork_, 7, 2);
    has_bitupdate(telegram, ignWork_, 7, 3);
    has_bitupdate(telegram, oilPreHeat_, 7, 4);
    has_bitupdate(telegram, heatingPump_, 7, 5);
    has_bitupdate(telegram, ww3wayValve_, 7, 6);
    has_bitupdate(telegram, wwCirc_, 7, 7);

    // dhw storage sensors (if present)
    // wwStorageTemp2 is also used by some brands as the boiler temperature - see https://github.com/emsesp/EMS-ESP/issues/206
    has_update(telegram, wwStorageTemp1_, 9);  // 0x8300 if not available
    has_update(telegram, wwStorageTemp2_, 11); // 0x8000 if not available - this is boiler temp

    has_update(telegram, retTemp_, 13);
    has_update(telegram, flameCurr_, 15);

    // system pressure. FF means missing
    has_update(telegram, sysPress_, 17); // is *10

    // read the service code / installation status as appears on the display
    if ((telegram->message_length > 18) && (telegram->offset == 0)) {
        char serviceCode[4];
        telegram->read_value(serviceCode[0], 18);
        serviceCode[0] = (serviceCode[0] == (char)0xF0) ? '~' : serviceCode[0];
        telegram->read_value(serviceCode[1], 19);
        serviceCode[2] = '\0'; // null terminate string
        has_update(serviceCode_, serviceCode, sizeof(serviceCode_));
    }

    has_update(telegram, serviceCodeNumber_, 20);

    check_active(); // do a quick check to see if the hot water or heating is active
}

/*
 * UBATotalUptime - type 0x14 - total uptime
 * received only after requested (not broadcasted)
 */
void Boiler::process_UBATotalUptime(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, UBAuptime_, 0, 3); // force to 3 bytes
}

/*
 * UBAParameters - type 0x16
 * data: FF 5A 64 00 0A FA 0F 02 06 64 64 02 08 F8 0F 0F 0F 0F 1E 05 04 09 09 00 28 00 3C
 */
void Boiler::process_UBAParameters(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, heatingActivated_, 0);
    has_update(telegram, heatingTemp_, 1);
    has_update(telegram, burnMaxPower_, 2);
    has_update(telegram, burnMinPower_, 3);
    has_update(telegram, boilHystOff_, 4);
    has_update(telegram, boilHystOn_, 5);
    has_update(telegram, burnMinPeriod_, 6);
    //has_update(telegram, pumpType_, 7); // 0=off, 02=?
    has_update(telegram, pumpDelay_, 8);
    has_update(telegram, pumpModMax_, 9);
    has_update(telegram, pumpModMin_, 10);
}

/*
 * UBASettingsWW - type 0x26 - max power on offset 7, #740
 * Boiler(0x08) -> Me(0x0B), ?(0x26), data: 01 05 00 0F 00 1E 58 5A
 */
void Boiler::process_UBASettingsWW(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, wwMaxPower_, 7);
}

// 0x33
//  Boiler(0x08) -> Me(0x0B), UBAParameterWW(0x33), data: 08 FF 30 FB FF 28 FF 07 46 00 00
void Boiler::process_UBAParameterWW(std::shared_ptr<const Telegram> telegram) {
    // has_bitupdate(telegram, wwEquipt_,0,3);  //  8=boiler has ww
    has_update(telegram, wwActivated_, 1); // 0xFF means on
    has_update(telegram, wwSelTemp_, 2);
    has_update(telegram, wwHystOn_, 3);         // Hyst on (default -5)
    has_update(telegram, wwHystOff_, 4);        // Hyst off (default -1)
    has_update(telegram, wwFlowTempOffset_, 5); // default 40
    has_update(telegram, wwCircPump_, 6);       // 0xFF means on
    has_update(telegram, wwCircMode_, 7);       // 1=1x3min 6=6x3min 7=continuous
    has_update(telegram, wwDisinfectionTemp_, 8);
    has_bitupdate(telegram, wwChargeType_, 10, 0); // 0 = charge pump, 0xff = 3-way valve

    uint8_t wwComfort;
    telegram->read_value(wwComfort, 9);
    if (wwComfort == 0) {
        wwComfort = 0; // Hot
    } else if (wwComfort == 0xD8) {
        wwComfort = 1; // Eco
    } else if (wwComfort == 0xEC) {
        wwComfort = 2; // Intelligent
    } else {
        wwComfort = EMS_VALUE_UINT_NOTSET;
    }
    has_update(wwComfort_, wwComfort);
}

/*
 * UBAMonitorWW - type 0x34 - dhw monitor. 19 bytes long
 * received every 10 seconds
 * Boiler(0x08) -> Me(0x0B), UBAMonitorWW(0x34), data: 30 01 BA 7D 00 21 00 00 03 00 01 22 2B 00 19 5B
*/
void Boiler::process_UBAMonitorWW(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, wwSetTemp_, 0);
    has_update(telegram, wwCurTemp_, 1);
    has_update(telegram, wwCurTemp2_, 3);

    has_update(telegram, wwType_, 8);
    has_update(telegram, wwCurFlow_, 9);
    has_update(telegram, wwWorkM_, 10, 3);  // force to 3 bytes
    has_update(telegram, wwStarts_, 13, 3); // force to 3 bytes

    has_bitupdate(telegram, wwOneTime_, 5, 1);
    has_bitupdate(telegram, wwDisinfect_, 5, 2);
    has_bitupdate(telegram, wwCharging_, 5, 3);
    has_bitupdate(telegram, wwRecharging_, 5, 4);
    has_bitupdate(telegram, wwTempOK_, 5, 5);
    has_bitupdate(telegram, wwActive_, 5, 6);
}

/*
 * UBAMonitorFastPlus - type 0xE4 - central heating monitor EMS+
 * temperatures at 7 and 23 always identical
+ * Bosch Logamax Plus GB122: issue #620
+ * 88 00 E4 00 00 2D 2D 00 00 C9 34 02 21 64 3D 05 02 01 DE 00 00 00 00 03 62 14 00 02 21 00 00 00 00 00 00 00 2B 2B 83
+ * GB125/Logamatic MC110: issue #650: add retTemp & sysPress
+ * 08 00 E4 00 10 20 2D 48 00 C8 38 02 37 3C 27 03 00 00 00 00 00 01 7B 01 8F 11 00 02 37 80 00 02 1B 80 00 7F FF 80 00
 */
void Boiler::process_UBAMonitorFastPlus(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, selFlowTemp_, 6);
    has_bitupdate(telegram, burnGas_, 11, 0);
    //has_bitupdate(telegram, heatingPump_, 11, 1); // heating active? see SlowPlus
    has_bitupdate(telegram, ww3wayValve_, 11, 2);
    has_update(telegram, curBurnPow_, 10);
    has_update(telegram, selBurnPow_, 9);
    has_update(telegram, curFlowTemp_, 7);
    has_update(telegram, flameCurr_, 19);
    has_update(telegram, retTemp_, 17); // can be 0 if no sensor, handled in export_values
    has_update(telegram, sysPress_, 21);

    //has_update(telegram, temperatur_, 13); // unknown temperature
    //has_update(telegram, temperatur_, 27); // unknown temperature

    // read 3 char service code / installation status as appears on the display
    if ((telegram->message_length > 3) && (telegram->offset == 0)) {
        char serviceCode[4];
        telegram->read_value(serviceCode[0], 1);
        serviceCode[0] = (serviceCode[0] == (char)0xF0) ? '~' : serviceCode[0];
        telegram->read_value(serviceCode[1], 2);
        telegram->read_value(serviceCode[2], 3);
        serviceCode[3] = '\0';
        has_update(serviceCode_, serviceCode, sizeof(serviceCode_));
    }

    has_update(telegram, serviceCodeNumber_, 4);

    // at this point do a quick check to see if the hot water or heating is active
    uint8_t state = EMS_VALUE_UINT_NOTSET;
    if (telegram->read_value(state, 11)) {
        boilerState_ = state & 0x01 ? 0x08 : 0;
        boilerState_ |= state & 0x02 ? 0x01 : 0;
        boilerState_ |= state & 0x04 ? 0x02 : 0;
    }

    check_active(); // do a quick check to see if the hot water or heating is active
}

/*
 * UBAMonitorSlow - type 0x19 - central heating monitor part 2 (27 bytes long)
 * received every 60 seconds
 * e.g. 08 00 19 00 80 00 02 41 80 00 00 00 00 00 03 91 7B 05 B8 40 00 00 00 04 92 AD 00 5E EE 80 00
 *      08 0B 19 00 FF EA 02 47 80 00 00 00 00 62 03 CA 24 2C D6 23 00 00 00 27 4A B6 03 6E 43 
 *                  00 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 17 19 20 21 22 23 24
 */
void Boiler::process_UBAMonitorSlow(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, outdoorTemp_, 0);
    has_update(telegram, boilTemp_, 2);
    has_update(telegram, exhaustTemp_, 4);
    has_update(telegram, switchTemp_, 25); // only if there is a mixer module present
    has_update(telegram, heatingPumpMod_, 9);
    has_update(telegram, burnStarts_, 10, 3);  // force to 3 bytes
    has_update(telegram, burnWorkMin_, 13, 3); // force to 3 bytes
    has_update(telegram, heatWorkMin_, 19, 3); // force to 3 bytes
}

/*
 * UBAMonitorSlowPlus2 - type 0xE3
 * 88 00 E3 00 04 00 00 00 00 01 00 00 00 00 00 02 22 2B 64 46 01 00 00 61
 */
void Boiler::process_UBAMonitorSlowPlus2(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, heatingPump2Mod_, 13); // Heat Pump Modulation
}

/*
 * UBAMonitorSlowPlus - type 0xE5 - central heating monitor EMS+
 * Boiler(0x08) -> Me(0x0B), UBAMonitorSlowPlus(0xE5),
 * data: 01 00 20 00 00 78 00 00 00 00 00 1E EB 00 9D 3E 00 00 00 00 6B 5E 00 06 4C 64 00 00 00 00 8A A3
 */
void Boiler::process_UBAMonitorSlowPlus(std::shared_ptr<const Telegram> telegram) {
    has_bitupdate(telegram, fanWork_, 2, 2);
    has_bitupdate(telegram, ignWork_, 2, 3);
    has_bitupdate(telegram, heatingPump_, 2, 5);
    has_bitupdate(telegram, wwCirc_, 2, 7);
    has_update(telegram, exhaustTemp_, 6);
    has_update(telegram, burnStarts_, 10, 3);  // force to 3 bytes
    has_update(telegram, burnWorkMin_, 13, 3); // force to 3 bytes
    has_update(telegram, heatWorkMin_, 19, 3); // force to 3 bytes
    has_update(telegram, heatingPumpMod_, 25);
    // temperature measurements at 4, see #620
}

/*
 * UBAParametersPlus - type 0xE6
 * parameters originally taken from
 * https://github.com/Th3M3/buderus_ems-wiki/blob/master/Einstellungen%20des%20Regelger%C3%A4ts%20MC110.md
 * 88 0B E6 00 01 46 00 00 46 0A 00 01 06 FA 0A 01 02 64 01 00 00 1E 00 3C 01 00 00 00 01 00 9A
 * from: issue #732
 *       data: 01 50 1E 5A 46 12 64 00 06 FA 3C 03 05 64 00 00 00 28 00 41 03 00 00 00 00 00 00 00 00 00
 */
void Boiler::process_UBAParametersPlus(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, heatingActivated_, 0);
    has_update(telegram, heatingTemp_, 1);
    has_update(telegram, burnMaxPower_, 4);
    has_update(telegram, burnMinPower_, 5);
    has_update(telegram, boilHystOff_, 8);
    has_update(telegram, boilHystOn_, 9);
    has_update(telegram, burnMinPeriod_, 10);
    // has_update(telegram, pumpType_, 11);   // guess, RC300 manual: power controlled, pressure controlled 1-4?
    // has_update(telegram, pumpDelay_, 12);  // guess
    // has_update(telegram, pumpModMax_, 13); // guess
    // has_update(telegram, pumpModMin_, 14); // guess
}

// 0xEA
// Boiler(0x08) -> (0x0B), (0xEA), data: 00 00 00 00 00 00 3C FB 00 28 00 02 46 00 00 00 3C 3C 28
void Boiler::process_UBAParameterWWPlus(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, wwSelTempOff_, 0); // confusing description in #96
    has_update(telegram, wwActivated_, 5);  // 0x01 means on
    has_update(telegram, wwSelTemp_, 6);    // setting here
    has_update(telegram, wwHystOn_, 7);
    has_update(telegram, wwHystOff_, 8);
    has_update(telegram, wwCircPump_, 10);         // 0x01 means yes
    has_update(telegram, wwCircMode_, 11);         // 1=1x3min... 6=6x3min, 7=continuous
    has_update(telegram, wwDisinfectionTemp_, 12); // setting here, status in E9
    has_update(telegram, wwSelTempSingle_, 16);
    has_update(telegram, wwSelTempLow_, 18);
}

// 0xE9 - WW monitor ems+
// e.g. 08 00 E9 00 37 01 F6 01 ED 00 00 00 00 41 3C 00 00 00 00 00 00 00 00 00 00 00 00 37 00 00 00 (CRC=77) #data=27
void Boiler::process_UBAMonitorWWPlus(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, wwSetTemp_, 0);
    has_update(telegram, wwCurTemp_, 1);
    has_update(telegram, wwCurTemp2_, 3);

    has_update(telegram, wwWorkM_, 14, 3);  // force to 3 bytes
    has_update(telegram, wwStarts_, 17, 3); // force to 3 bytes

    has_bitupdate(telegram, wwOneTime_, 12, 2);
    has_bitupdate(telegram, wwDisinfect_, 12, 3);
    has_bitupdate(telegram, wwCharging_, 12, 4);
    has_bitupdate(telegram, wwRecharging_, 13, 4);
    has_bitupdate(telegram, wwTempOK_, 13, 5);
    has_bitupdate(telegram, wwCirc_, 13, 2);

    // has_update(telegram, wwActivated_, 20); // Activated is in 0xEA, this is something other 0/100%
    // has_update(telegram, wwSelTemp_, 10);   // this is wrong, see #96
    // has_update(telegram, wwDisinfectionTemp_, 9);
}

/*
 * UBAInformation - type 0x495
 * all values 32 bit
 * 08 00 FF 00 03 95 00 0F 8E C2 00 08 39 C8 00 00 18 7A 00 07 3C 80 00 00 00 00 00 00 00 E5 F6 00
 * 08 00 FF 18 03 95 00 00 00 A1 00 00 00 00 00 00 00 44 00 00 00 00 00 00 00 0A 00 00 00 0A BD 00
 * 08 00 FF 30 03 95 00 00 00 00 00 00 00 00 00 00 02 10 00 00 00 00 00 00 02 1A 00 00 00 02 66 00
 * 08 00 FF 48 03 95 00 00 01 15 00 00 00 00 00 00 00 F9 29 00
 *
 */
void Boiler::process_UBAInformation(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, upTimeControl_, 0);
    has_update(telegram, upTimeCompHeating_, 8);
    has_update(telegram, upTimeCompCooling_, 16);
    has_update(telegram, upTimeCompWw_, 4);
    has_update(telegram, upTimeCompPool_, 12);

    has_update(telegram, totalCompStarts_, 20);
    has_update(telegram, heatingStarts_, 28);
    has_update(telegram, coolingStarts_, 36);
    has_update(telegram, wwStarts2_, 24);
    has_update(telegram, poolStarts_, 32);

    has_update(telegram, nrgConsTotal_, 64);

    has_update(telegram, auxElecHeatNrgConsTotal_, 40);
    has_update(telegram, auxElecHeatNrgConsHeating_, 48);
    has_update(telegram, auxElecHeatNrgConsWW_, 44);
    has_update(telegram, auxElecHeatNrgConsPool_, 52);

    has_update(telegram, nrgConsCompTotal_, 56);
    has_update(telegram, nrgConsCompHeating_, 68);
    has_update(telegram, nrgConsCompWw_, 72);
    has_update(telegram, nrgConsCompCooling_, 76);
    has_update(telegram, nrgConsCompPool_, 80);
}

/*
 * UBAEnergy - type 0x494
 * Energy-values all 32bit
 * 08 00 FF 00 03 94 03 31 21 59 00 00 7C 70 00 00 15 B8 00 00 40 E3 00 00 27 23 FF FF FF FF EA
 * 08 00 FF 18 03 94 FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00 7E
 * 08 00 FF 31 03 94 00 00 00 00 00 00 00 38
 */
void Boiler::process_UBAEnergySupplied(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, nrgSuppTotal_, 4);
    has_update(telegram, nrgSuppHeating_, 12);
    has_update(telegram, nrgSuppWw_, 8);
    has_update(telegram, nrgSuppCooling_, 16);
    has_update(telegram, nrgSuppPool_, 20);
}

// Heatpump power - type 0x48D
//08 00 FF 00 03 8D 03 00 10 30 10 60 00 04 00 00 00 17 00 00 00 3C 38 0E 64 00 00 0C 33 C7 00
//XR1A050001   A05 Pump Heat circuit (1.0 ) 1 >> 1 & 0x01 ?
//XR1A040001   A04 Pump Cold circuit (1.0 ) 1 & 0x1 ?
void Boiler::process_HpPower(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, hpPower_, 11);
    has_bitupdate(telegram, hpCompOn_, 3, 4);
    has_update(telegram, hpBrinePumpSpd_, 5);
    has_update(telegram, hpCompSpd_, 17);
    has_update(telegram, hpCircSpd_, 4);
    has_bitupdate(telegram, hpSwitchValve_, 0, 4);
    has_update(telegram, hpActivity_, 7);

    has_update(hpHeatingOn_, hpActivity_ == 1 ? 0xFF : 0);
    has_update(hpCoolingOn_, hpActivity_ == 2 ? 0xFF : 0);
    has_update(hpWwOn_, hpActivity_ == 3 ? 0xFF : 0);
    has_update(hpPoolOn_, hpActivity_ == 4 ? 0xFF : 0);
}

// Heatpump outdoor unit - type 0x48F
void Boiler::process_HpOutdoor(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, hpTc0_, 6);
    has_update(telegram, hpTc1_, 4);
    has_update(telegram, hpTc3_, 2);
    has_update(telegram, hpTr3_, 16);
    has_update(telegram, hpTr4_, 18);
    // has_update(telegram, hpTr5_, 20);
    // has_update(telegram, hpTr6_, 0);
    has_update(telegram, hpTr7_, 30);
    has_update(telegram, hpTl2_, 12);
    has_update(telegram, hpPl1_, 26);
    has_update(telegram, hpPh1_, 28);
    has_update(telegram, hpBrineIn_, 8);
    has_update(telegram, hpBrineOut_, 10);
    has_update(telegram, hpSuctionGas_, 20);
    has_update(telegram, hpHotGas_, 0);
}

// Heatpump pool unit - type 0x48A
// 08 00 FF 00 03 8A 01 4C 01 0C 00 00 0A 00 1E 00 00 01 00 04 4A 00
void Boiler::process_HpPool(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, poolSetTemp_, 1);
}


// 0x2A - MC110Status
// e.g. 88 00 2A 00 00 00 00 00 00 00 00 00 D2 00 00 80 00 00 01 08 80 00 02 47 00
// see https://github.com/emsesp/EMS-ESP/issues/397
void Boiler::process_MC110Status(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, wwMixerTemp_, 14);
    has_update(telegram, wwCylMiddleTemp_, 18);
}

/*
 * UBAOutdoorTemp - type 0xD1 - external temperature EMS+
 */
void Boiler::process_UBAOutdoorTemp(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, outdoorTemp_, 0);
}

// UBASetPoint 0x1A
void Boiler::process_UBASetPoints(std::shared_ptr<const Telegram> telegram) {
    has_update(telegram, setFlowTemp_, 0);    // boiler set temp from thermostat
    has_update(telegram, setBurnPow_, 1);     // max burner power in %
    has_update(telegram, wwSetPumpPower_, 2); // ww pump speed/power?
}

// 0x6DC, ff for cascaded heatsources (hs)
void Boiler::process_CascadeMessage(std::shared_ptr<const Telegram> telegram) {
    // uint8_t  hsActivated;
    // has_update(telegram, hsActivated, 0);
    telegram->read_value(burnWorkMin_, 3); // this is in seconds
    burnWorkMin_ /= 60;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// 0x35 - not yet implemented
void Boiler::process_UBAFlags(std::shared_ptr<const Telegram> telegram) {
}

#pragma GCC diagnostic pop

// 0x1C
// 08 00 1C 94 0B 0A 1D 31 08 00 80 00 00 00 -> message for 29.11.2020
// 08 00 1C 94 0B 0A 1D 31 00 00 00 00 00 00 -> message reset
void Boiler::process_UBAMaintenanceStatus(std::shared_ptr<const Telegram> telegram) {
    // 5. byte: Maintenance due (0 = no, 3 = yes, due to operating hours, 8 = yes, due to date)
    uint8_t message_code = maintenanceMessage_[2] - '0';
    telegram->read_value(message_code, 5);

    if (Helpers::hasValue(message_code)) {
        char message[5];
        snprintf(message, sizeof(message), "H%02d", message_code);
        has_update(maintenanceMessage_, message, sizeof(maintenanceMessage_));
    }
}

// 0x10, 0x11
void Boiler::process_UBAErrorMessage(std::shared_ptr<const Telegram> telegram) {
    if (telegram->offset > 0 || telegram->message_length < 11) {
        return;
    }
    // data: displaycode(2), errornumber(2), year, month, hour, day, minute, duration(2), src-addr
    if (telegram->message_data[4] & 0x80) { // valid date

        static uint32_t lastCodeDate_ = 0; // last code date
        char            code[3];
        uint16_t        codeNo;
        code[0] = telegram->message_data[0];
        code[1] = telegram->message_data[1];
        code[2] = 0;
        telegram->read_value(codeNo, 2);
        uint16_t year  = (telegram->message_data[4] & 0x7F) + 2000;
        uint8_t  month = telegram->message_data[5];
        uint8_t  day   = telegram->message_data[7];
        uint8_t  hour  = telegram->message_data[6];
        uint8_t  min   = telegram->message_data[8];
        uint32_t date  = (year - 2000) * 535680UL + month * 44640UL + day * 1440UL + hour * 60 + min;
        uint16_t duration;
        telegram->read_value(duration, 9);
        // store only the newest code from telegrams 10 and 11
        if (date > lastCodeDate_) {
            lastCodeDate_ = date;
            snprintf(lastCode_, sizeof(lastCode_), "%s(%d) %02d.%02d.%d %02d:%02d (%d min)", code, codeNo, day, month, year, hour, min, duration);
            has_update(lastCode_);
        }
    }
}

// 0xC2, without clock in system it stores 3 bytes uptime in 11 and 16, with clock date in 10-14, and 15-19
// date is marked with 0x80 to year-field
void Boiler::process_UBAErrorMessage2(std::shared_ptr<const Telegram> telegram) {
    if (telegram->offset > 0 || telegram->message_length < 20) {
        return;
    }

    char     code[sizeof(lastCode_)];
    uint16_t codeNo;
    code[0] = telegram->message_data[5];
    code[1] = telegram->message_data[6];
    code[2] = telegram->message_data[7];
    code[3] = 0;
    telegram->read_value(codeNo, 8);

    // check for valid date, https://github.com/emsesp/EMS-ESP32/issues/204
    if (telegram->message_data[10] & 0x80) {
        uint16_t start_year  = (telegram->message_data[10] & 0x7F) + 2000;
        uint8_t  start_month = telegram->message_data[11];
        uint8_t  start_day   = telegram->message_data[13];
        uint8_t  start_hour  = telegram->message_data[12];
        uint8_t  start_min   = telegram->message_data[14];
        uint16_t end_year    = (telegram->message_data[15] & 0x7F) + 2000;
        uint8_t  end_month   = telegram->message_data[16];
        uint8_t  end_day     = telegram->message_data[18];
        uint8_t  end_hour    = telegram->message_data[17];
        uint8_t  end_min     = telegram->message_data[19];

        if (telegram->message_data[15] & 0x80) { //valid end date
            snprintf(&code[3],
                     sizeof(code) - 3,
                     "(%d) %02d.%02d.%04d %02d:%02d - %02d.%02d.%04d %02d:%02d",
                     codeNo,
                     start_day,
                     start_month,
                     start_year,
                     start_hour,
                     start_min,
                     end_day,
                     end_month,
                     end_year,
                     end_hour,
                     end_min);
        } else { // no valid end date means error still persists
            snprintf(&code[3], sizeof(code) - 3, "(%d) %02d.%02d.%04d %02d:%02d - now", codeNo, start_day, start_month, start_year, start_hour, start_min);
        }
    } else { // no clock, the uptime is stored https://github.com/emsesp/EMS-ESP32/issues/121
        uint32_t starttime, endtime;
        telegram->read_value(starttime, 11, 3);
        telegram->read_value(endtime, 16, 3);
        snprintf(&code[3], sizeof(code) - 3, "(%d) @uptime %d - %d min", codeNo, starttime, endtime);
    }
    has_update(lastCode_, code, sizeof(lastCode_));
}

// 0x15 maintenance data
void Boiler::process_UBAMaintenanceData(std::shared_ptr<const Telegram> telegram) {
    if (telegram->offset > 0 || telegram->message_length < 5) {
        return;
    }

    // added additional type 3 (for Nefit TrendLine HRC 30/CW5)
    has_update(telegram, maintenanceType_, 0); // 0 = off, 1 = by operating hours, 2 = by date, 3 = manual

    uint8_t time = (maintenanceTime_ == EMS_VALUE_USHORT_NOTSET) ? EMS_VALUE_UINT_NOTSET : maintenanceTime_ / 100;
    telegram->read_value(time, 1);
    if (Helpers::hasValue(time)) {
        if (time * 100 != maintenanceTime_) {
            maintenanceTime_ = time * 100;
            has_update(&maintenanceTime_);
        }
    }

    // date only
    uint8_t day   = telegram->message_data[2];
    uint8_t month = telegram->message_data[3];
    uint8_t year  = telegram->message_data[4];
    if (day > 0 && month > 0) {
        char date[20];
        snprintf(date, sizeof(date), "%02d.%02d.%04d", day, month, year + 2000);
        has_update(maintenanceDate_, date, sizeof(maintenanceDate_));
    }
}

// Set the dhw temperature 0x33/0x35 or 0xEA
bool Boiler::set_ww_temp(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        // write_command(EMS_TYPE_UBAFlags, 3, v, EMS_TYPE_UBAParameterWWPlus); // test for #96
        write_command(EMS_TYPE_UBAParameterWWPlus, 6, v, EMS_TYPE_UBAParameterWWPlus);
    } else {
        // some boiler have it in UBAParametersWW 0x33/0xEA, some in 0x35
        write_command(EMS_TYPE_UBAFlags, 3, v, EMS_TYPE_UBAParameterWW);       // for i9000, see #397
        write_command(EMS_TYPE_UBAParameterWW, 2, v, EMS_TYPE_UBAParameterWW); // read seltemp back
    }

    return true;
}


// Set the lower dhw temperature 0xEA
bool Boiler::set_ww_temp_low(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }

    write_command(EMS_TYPE_UBAParameterWWPlus, 18, v, EMS_TYPE_UBAParameterWWPlus);
    return true;
}

// Set the dhw single charge temperature 0xEA
bool Boiler::set_ww_temp_single(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }

    write_command(EMS_TYPE_UBAParameterWWPlus, 16, v, EMS_TYPE_UBAParameterWWPlus);
    return true;
}

// Set the dhw disinfection temperature
bool Boiler::set_ww_disinfect_temp(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParameterWWPlus, 12, v, EMS_TYPE_UBAParameterWWPlus);
    } else {
        write_command(EMS_TYPE_UBAParameterWW, 8, v, EMS_TYPE_UBAParameterWW);
    }

    return true;
}

// flow temp
bool Boiler::set_flow_temp(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }

    if (has_telegram_id(0xE4)) {
        write_command(EMS_TYPE_UBASetPoints, 0, v, 0xE4);
    } else {
        write_command(EMS_TYPE_UBASetPoints, 0, v, 0x18);
    }
    return true;
}

// set selected burner power
bool Boiler::set_burn_power(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    write_command(EMS_TYPE_UBASetPoints, 1, v, EMS_TYPE_UBASetPoints);

    return true;
}

// Set the dhw flow temperature offset 0x33
bool Boiler::set_ww_flowTempOffset(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v, true)) {
        return false;
    }

    write_command(EMS_TYPE_UBAParameterWW, 5, v, EMS_TYPE_UBAParameterWW);

    return true;
}

// set heating activated
bool Boiler::set_heating_activated(const char * value, const int8_t id) {
    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 0, v ? 0x01 : 0, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 0, v ? 0xFF : 0, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set heating maximum temperature
bool Boiler::set_heating_temp(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 1, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 1, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set min boiler output
bool Boiler::set_min_power(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 5, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 3, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set max boiler output
bool Boiler::set_max_power(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 4, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 2, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set ww on hysteresis
bool Boiler::set_ww_hyst_on(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v, true)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAParameterWWPlus, 7, v, EMS_TYPE_UBAParameterWWPlus);
    } else {
        write_command(EMS_TYPE_UBAParameterWW, 3, v, EMS_TYPE_UBAParameterWW);
    }

    return true;
}

// set ww off hysteresis
bool Boiler::set_ww_hyst_off(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v, true)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAParameterWWPlus, 8, v, EMS_TYPE_UBAParameterWWPlus);
    } else {
        write_command(EMS_TYPE_UBAParameterWW, 4, v, EMS_TYPE_UBAParameterWW);
    }

    return true;
}

// set dhw max power
bool Boiler::set_ww_maxpower(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    write_command(EMS_TYPE_UBASettingsWW, 7, v, EMS_TYPE_UBASettingsWW);

    return true;
}

// set min pump modulation
bool Boiler::set_min_pump(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 14, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 10, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set max pump modulation
bool Boiler::set_max_pump(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 13, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 9, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set boiler on hysteresis
bool Boiler::set_hyst_on(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v, true)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 9, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 5, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set boiler off hysteresis
bool Boiler::set_hyst_off(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2temperature(value, v, true)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 8, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 4, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set min burner period
bool Boiler::set_burn_period(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParametersPlus)) {
        write_command(EMS_TYPE_UBAParametersPlus, 10, v, EMS_TYPE_UBAParametersPlus);
    } else {
        write_command(EMS_TYPE_UBAParameters, 6, v, EMS_TYPE_UBAParameters);
    }

    return true;
}

// set pump delay
bool Boiler::set_pump_delay(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameters)) {
        write_command(EMS_TYPE_UBAParameters, 8, v, EMS_TYPE_UBAParameters);
        return true;
    }

    return false;
}

// note some boilers do not have this setting, than it's done by thermostat
// on a RC35 it's by EMSESP::send_write_request(0x37, 0x10, 2, &set, 1, 0); (set is 1,2,3) 1=hot, 2=eco, 3=intelligent
bool Boiler::set_ww_mode(const char * value, const int8_t id) {
    uint8_t set;
    if (!Helpers::value2enum(value, set, FL_(enum_comfort))) {
        return false;
    }

    if (!is_fetch(EMS_TYPE_UBAParameterWW)) {
        return false;
    }

    if (set == 0) {
        // LOG_INFO(F("Setting boiler dhw to Hot"));
    } else if (set == 1) {
        // LOG_INFO(F("Setting boiler dhw to Eco"));
        set = 0xD8;
    } else if (set == 2) {
        // LOG_INFO(F("Setting boiler dhw to Intelligent"));
        set = 0xEC;
    } else {
        return false; // do nothing
    }

    write_command(EMS_TYPE_UBAParameterWW, 9, set, EMS_TYPE_UBAParameterWW);
    return true;
}

// turn on/off dhw
bool Boiler::set_ww_activated(const char * value, const int8_t id) {
    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    // https://github.com/emsesp/EMS-ESP/issues/268
    // 08 for HT3 seems to be wrong, see https://github.com/emsesp/EMS-ESP32/issues/89
    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAParameterWWPlus, 1, v ? 1 : 0, EMS_TYPE_UBAParameterWWPlus);
    } else {
        write_command(EMS_TYPE_UBAParameterWW, 1, v ? 0xFF : 0, EMS_TYPE_UBAParameterWW);
    }

    return true;
}

// Activate / De-activate the Warm Tap Water
// Note: Using the type 0x1D to put the boiler into Test mode. This may be shown on the boiler with a flashing 'T'
bool Boiler::set_tapwarmwater_activated(const char * value, const int8_t id) {
    if (!Helpers::hasValue(wwTapActivated_, EMS_VALUE_BOOL)) {
        return false;
    }

    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    uint8_t message_data[EMS_MAX_TELEGRAM_MESSAGE_LENGTH];
    for (uint8_t i = 0; i < sizeof(message_data); i++) {
        message_data[i] = 0x00;
    }

    // we use the special test mode 0x1D for this. Setting the first data to 5A puts the system into test mode and
    // a setting of 0x00 puts it back into normal operating mode
    // when in test mode we're able to mess around with the 3-way valve settings
    if (!v) {
        // DHW off
        message_data[0] = 0x5A; // test mode on
        message_data[1] = 0x00; // burner output 0%
        message_data[3] = 0x64; // boiler pump capacity 100%
        message_data[4] = 0xFF; // 3-way valve hot water only
        wwTapActivated_ = 0;
    } else {
        // get out of test mode. Send all zeros.
        // telegram: 0B 08 1D 00 00
        wwTapActivated_ = 1;
    }

    write_command(EMS_TYPE_UBAFunctionTest, 0, message_data, sizeof(message_data), 0);

    return true;
}

// Activate / De-activate One Time dhw 0x35
// true = on, false = off
// See also https://github.com/emsesp/EMS-ESP/issues/341#issuecomment-596245458 for Junkers
bool Boiler::set_ww_onetime(const char * value, const int8_t id) {
    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAFlags, 0, (v ? 0x22 : 0x02), 0xE9); // not sure if this is in flags
    } else {
        write_command(EMS_TYPE_UBAFlags, 0, (v ? 0x23 : 0x03), 0x34);
    }

    return true;
}

// starting dhw disinfect, set to off seems not working
bool Boiler::set_ww_disinfect(const char * value, const int8_t id) {
    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAFlags, 0, (v ? 0x44 : 0x04), 0xE9); // not sure if this is in flags
    } else {
        write_command(EMS_TYPE_UBAFlags, 0, (v ? 0x44 : 0x04), 0x34);
    }

    return true;
}

// Activate / De-activate circulation of dhw 0x35
// true = on, false = off
bool Boiler::set_ww_circulation(const char * value, const int8_t id) {
    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAFlags, 1, (v ? 0x22 : 0x02), 0xE9); // not sure if this is in flags
    } else {
        write_command(EMS_TYPE_UBAFlags, 1, (v ? 0x22 : 0x02), 0x34);
    }

    return true;
}

// configuration of dhw circulation pump
bool Boiler::set_ww_circulation_pump(const char * value, const int8_t id) {
    bool v = false;
    if (!Helpers::value2bool(value, v)) {
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAParameterWWPlus, 10, v ? 0x01 : 0x00, EMS_TYPE_UBAParameterWWPlus);
    } else {
        write_command(EMS_TYPE_UBAParameterWW, 6, v ? 0xFF : 0x00, EMS_TYPE_UBAParameterWW);
    }

    return true;
}

// Set the mode of circulation, 1x3min, ... 6x3min, continuous
// true = on, false = off
bool Boiler::set_ww_circulation_mode(const char * value, const int8_t id) {
    int v = 0;
    if (!Helpers::value2number(value, v)) {
        return false;
    }

    if (v < 7) {
        // LOG_INFO(F("Setting dhw circulation mode %dx3min"), v);
    } else if (v == 7) {
        // LOG_INFO(F("Setting dhw circulation mode continuous"));
    } else {
        // LOG_WARNING(F("Set dhw circulation mode: Invalid value"));
        return false;
    }

    if (is_fetch(EMS_TYPE_UBAParameterWWPlus)) {
        write_command(EMS_TYPE_UBAParameterWWPlus, 11, v, EMS_TYPE_UBAParameterWWPlus);
    } else {
        write_command(EMS_TYPE_UBAParameterWW, 7, v, EMS_TYPE_UBAParameterWW);
    }

    return true;
}

// Reset command
// 0 & 1        Reset-Mode (Manual, others)
// 8            reset maintenance message Hxx
// 12 & 13      Reset the Error-memory
bool Boiler::set_reset(const char * value, const int8_t id) {
    uint8_t num;
    if (!Helpers::value2enum(value, num, FL_(enum_reset))) {
        return false;
    }

    if (num == 1) {
        // LOG_INFO(F("Reset boiler maintenance message"));
        write_command(0x05, 0x08, 0xFF, 0x1C);
        has_update(reset_);
        return true;
    } else if (num == 2) {
        // LOG_INFO(F("Reset boiler error message"));
        write_command(0x05, 0x00, 0x5A); // error reset
        has_update(reset_);
        return true;
    }
    return false;
}

//maintenance
bool Boiler::set_maintenance(const char * value, const int8_t id) {
    std::string s;
    if (Helpers::value2string(value, s)) {
        if (s == Helpers::toLower(read_flash_string(F_(reset)))) {
            // LOG_INFO(F("Reset boiler maintenance message"));
            write_command(0x05, 0x08, 0xFF, 0x1C);
            return true;
        }
    }

    if (strlen(value) == 10) { // date
        uint8_t day   = (value[0] - '0') * 10 + (value[1] - '0');
        uint8_t month = (value[3] - '0') * 10 + (value[4] - '0');
        uint8_t year  = (uint8_t)(Helpers::atoint(&value[6]) - 2000);
        if (day > 0 && day < 32 && month > 0 && month < 13) {
            LOG_INFO(F("Setting maintenance date to %02d.%02d.%04d"), day, month, year + 2000);
            uint8_t data[5] = {2, (uint8_t)(maintenanceTime_ / 100), day, month, year};
            write_command(0x15, 0, data, 5, 0x15);
        } else {
            LOG_WARNING(F("Setting maintenance: wrong format %d.%d.%d"), day, month, year + 2000);
            return false;
        }
        return true;
    }

    int hrs;
    if (Helpers::value2number(value, hrs)) {
        if (hrs > 99 && hrs < 25600) {
            LOG_INFO(F("Setting maintenance time %d hours"), hrs);
            uint8_t data[2] = {1, (uint8_t)(hrs / 100)};
            write_command(0x15, 0, data, 2, 0x15);
            return true;
        }
    }

    uint8_t num;
    if (Helpers::value2enum(value, num, FL_(enum_off_time_date_manual))) {
        LOG_INFO(F("Setting maintenance type to %s"), value);
        write_command(0x15, 0, num, 0x15);
        return true;
    }

    LOG_WARNING(F("Setting maintenance: wrong format"));
    return false;
}
//maintenance
bool Boiler::set_maintenancetime(const char * value, const int8_t id) {
    int hrs;
    if (Helpers::value2number(value, hrs)) {
        if (hrs > 99 && hrs < 25600) {
            LOG_INFO(F("Setting maintenance time %d hours"), hrs);
            uint8_t data[2] = {1, (uint8_t)(hrs / 100)};
            write_command(0x15, 0, data, 2, 0x15);
            return true;
        }
    }
    LOG_WARNING(F("Setting maintenance: wrong format"));
    return false;
}

//maintenance
bool Boiler::set_maintenancedate(const char * value, const int8_t id) {
    if (strlen(value) == 10) { // date
        uint8_t day   = (value[0] - '0') * 10 + (value[1] - '0');
        uint8_t month = (value[3] - '0') * 10 + (value[4] - '0');
        uint8_t year  = (uint8_t)(Helpers::atoint(&value[6]) - 2000);
        if (day > 0 && day < 32 && month > 0 && month < 13) {
            LOG_INFO(F("Setting maintenance date to %02d.%02d.%04d"), day, month, year + 2000);
            uint8_t data[5] = {2, (uint8_t)(maintenanceTime_ / 100), day, month, year};
            write_command(0x15, 0, data, 5, 0x15);
        } else {
            LOG_WARNING(F("Setting maintenance: wrong format %d.%d.%d"), day, month, year + 2000);
            return false;
        }
        return true;
    }

    LOG_WARNING(F("Setting maintenance: wrong format"));
    return false;
}

// Set the pool temperature 0x48A
bool Boiler::set_pool_temp(const char * value, const int8_t id) {
    float v = 0;
    if (!Helpers::value2temperature(value, v)) {
        return false;
    }
    uint8_t v2 = ((v * 2) + 0.5);
    // LOG_INFO(F("Setting pool temperature to %d.%d C"), v2 >> 1, (v2 & 0x01) * 5);
    write_command(0x48A, 1, v2, 0x48A);

    return true;
}

} // namespace emsesp
