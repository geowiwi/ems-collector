/*
 * Buderus EMS data collector
 *
 * Copyright (C) 2011 Danny Baumann <dannybaumann@web.de>
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

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include "EmsMessage.h"
#include "Options.h"

#define BYTEFORMAT_HEX \
    "0x" << std::setbase(16) << std::setw(2) << std::setfill('0') << (unsigned int)
#define BYTEFORMAT_DEC \
    std::dec << (unsigned int)

EmsValue::EmsValue(Type type, SubType subType, const uint8_t *data, size_t len, int divider) :
    m_type(type),
    m_subType(subType),
    m_readingType(Numeric)
{
    int value = 0;
    for (size_t i = 0; i < len; i++) {
	value = (value << 8) | data[i];
    }

    /* treat values with highest bit set as negative
     * e.g. size = 2, value = 0xfffe -> real value -2
     */
    if (data[0] & 0x80) {
	value = value - (1 << (len * 8));
    }

    m_value = (float) value / (float) divider;
}

EmsValue::EmsValue(Type type, SubType subType, uint8_t value, uint8_t bit) :
    m_type(type),
    m_subType(subType),
    m_readingType(Boolean),
    m_value((value & (1 << bit)) != 0)
{
}

EmsValue::EmsValue(Type type, SubType subType, uint8_t low, uint8_t medium, uint8_t high) :
    m_type(type),
    m_subType(subType),
    m_readingType(Kennlinie),
    m_value(std::vector<uint8_t>({ low, medium, high }))
{
}

EmsValue::EmsValue(Type type, SubType subType, uint8_t value) :
    m_type(type),
    m_subType(subType),
    m_readingType(Enumeration),
    m_value(value)
{
}

EmsValue::EmsValue(Type type, SubType subType, const ErrorEntry& error) :
    m_type(type),
    m_subType(subType),
    m_readingType(Error),
    m_value(error)
{
}

EmsValue::EmsValue(Type type, SubType subType, const EmsProto::SystemTimeRecord& record) :
    m_type(type),
    m_subType(subType),
    m_readingType(SystemTime),
    m_value(record)
{
}

EmsValue::EmsValue(Type type, SubType subType, const std::string& value) :
    m_type(type),
    m_subType(subType),
    m_readingType(Formatted),
    m_value(value)
{
}

EmsMessage::EmsMessage(ValueHandler& valueHandler, const std::vector<uint8_t>& data) :
    m_valueHandler(valueHandler),
    m_data(data)
{
    if (m_data.size() >= 4) {
	m_source = m_data[0];
	m_dest = m_data[1];
	m_type = m_data[2];
	m_offset = m_data[3];
	m_data.erase(m_data.begin(), m_data.begin() + 4);
    } else {
	m_source = 0;
	m_dest = 0;
	m_type = 0;
	m_offset = 0;
    }
}

EmsMessage::EmsMessage(uint8_t dest, uint8_t type, uint8_t offset,
		       const std::vector<uint8_t>& data,
		       bool expectResponse) :
    m_valueHandler(),
    m_data(data),
    m_source(EmsProto::addressPC),
    m_dest(dest | (expectResponse ? 0x80 : 0)),
    m_type(type),
    m_offset(offset)
{
}

std::vector<uint8_t>
EmsMessage::getSendData() const
{
    std::vector<uint8_t> data;

    /* own address omitted on send */
    data.push_back(m_dest);
    data.push_back(m_type);
    data.push_back(m_offset);
    data.insert(data.end(), m_data.begin(), m_data.end());

    return data;
}

void
EmsMessage::handle()
{
    bool handled = false;
    DebugStream& debug = Options::messageDebug();

    if (debug) {
	time_t now = time(NULL);
	struct tm time;

	localtime_r(&now, &time);
	debug << std::dec << "MESSAGE[";
	debug << std::setw(2) << std::setfill('0') << time.tm_mday;
	debug << "." << std::setw(2) << std::setfill('0') << (time.tm_mon + 1);
	debug << "." << (time.tm_year + 1900) << " ";
	debug << std::setw(2) << std::setfill('0') << time.tm_hour;
	debug << ":" << std::setw(2) << std::setfill('0') << time.tm_min;
	debug << ":" << std::setw(2) << std::setfill('0') << time.tm_sec;
	debug << "]: source " << BYTEFORMAT_HEX m_source;
	debug << ", dest " << BYTEFORMAT_HEX m_dest;
	debug << ", type " << BYTEFORMAT_HEX m_type;
	debug << ", offset " << BYTEFORMAT_DEC m_offset;
	debug << ", data ";
	for (size_t i = 0; i < m_data.size(); i++) {
	    debug << " " << BYTEFORMAT_HEX m_data[i];
	}
	debug << std::endl;
    }

    if (!m_valueHandler) {
	/* kind of pointless to parse in that case */
	return;
    }

    if (!m_source && !m_dest && !m_type) {
	/* invalid packet */
	return;
    }

    if (m_dest & 0x80) {
	/* if highest bit of dest is set, it's a polling request -> ignore */
	return;
    }

    switch (m_source) {
	case EmsProto::addressUBA:
	    /* UBA message */
	    switch (m_type) {
		case 0x07:
		    /* yet unknown contents:
		     * 0x8 0x0 0x7 0x0 0x3 0x3 0x0 0x2 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0 */
		    break;
		case 0x10:
		case 0x11:
		    parseUBAErrorMessage();
		    handled = true;
		    break;
		case 0x16: parseUBAParametersMessage(); handled = true; break;
		case 0x18: parseUBAMonitorFastMessage(); handled = true; break;
		case 0x19: parseUBAMonitorSlowMessage(); handled = true; break;
		case 0x1c:
		    /* unknown message with varying length
		     * 0x8 0x10 0x1c 0x0 0x8a 0x4 0x13 0x1c 0x1d 0x0 0x0 0x0
		     * 0x8 0x10 0x1c 0x8
		     */
		    break;
		case 0x33: parseUBAParameterWWMessage(); handled = true; break;
		case 0x34: parseUBAMonitorWWMessage(); handled = true; break;
	    }
	    break;
	case EmsProto::addressBC10:
	    /* BC10 message */
	    switch (m_type) {
		case 0x29:
		    /* yet unknown: 0x9 0x10 0x29 0x0 0x6b */
		    break;
	    }
	    break;
	case EmsProto::addressRC:
	    /* RC message */
	    switch (m_type) {
		case 0x06: parseRCTimeMessage(); handled = true; break;
		case 0x1A: /* command for UBA3 */ handled = true; break;
		case 0x35: /* command for UBA3 */ handled = true; break;
		case 0x3E:
		    parseRCHKMonitorMessage(EmsValue::HK1);
		    handled = true;
		    break;
		case 0x48:
		    parseRCHKMonitorMessage(EmsValue::HK2);
		    handled = true;
		    break;
		case 0x9D: /* command for WM10 */ handled = true; break;
		case 0xA2: /* unknown, 11 zeros */ break;
		case 0xA3: parseRCOutdoorTempMessage(); handled = true; break;
		case 0xAC: /* command for MM10 */ handled = true; break;
	    }
	case EmsProto::addressWM10:
	    /* WM10 message */
	    switch (m_type) {
		case 0x9C: parseWMTemp1Message(); handled = true; break;
		case 0x1E: parseWMTemp2Message(); handled = true; break;
	    }
	    break;
	case EmsProto::addressMM10:
	    /* MM10 message */
	    switch (m_type) {
		case 0xAB: parseMMTempMessage(); handled = true; break;
	    }
	    break;
    }

    if (!handled) {
	DebugStream& dataDebug = Options::dataDebug();
	if (dataDebug) {
	    dataDebug << "DATA: Unhandled message received";
	    dataDebug << "(source " << BYTEFORMAT_HEX m_source << ", type ";
	    dataDebug << BYTEFORMAT_HEX m_type << ")." << std::endl;
	}
    }
}

void
EmsMessage::parseNumeric(size_t offset, size_t size, int divider,
			 EmsValue::Type type, EmsValue::SubType subtype)
{
    if (canAccess(offset, size)) {
	EmsValue value(type, subtype, &m_data.at(offset - m_offset), size, divider);
	m_valueHandler(value);
    }
}

void
EmsMessage::parseBool(size_t offset, uint8_t bit,
		      EmsValue::Type type, EmsValue::SubType subtype)
{
    if (canAccess(offset, 1)) {
	EmsValue value(type, subtype, m_data.at(offset - m_offset), bit);
	m_valueHandler(value);
    }
}

void
EmsMessage::parseUBAMonitorFastMessage()
{
    parseNumeric(0, 1, 1, EmsValue::SollTemp, EmsValue::Kessel);
    parseNumeric(1, 2, 10, EmsValue::IstTemp, EmsValue::Kessel);
    parseNumeric(11, 2, 10, EmsValue::IstTemp, EmsValue::WW);
    parseNumeric(13, 2, 10, EmsValue::IstTemp, EmsValue::Ruecklauf);
    parseNumeric(3, 1, 1, EmsValue::MaxLeistung, EmsValue::None);
    parseNumeric(4, 1, 1, EmsValue::MomLeistung, EmsValue::None);
    parseNumeric(15, 2, 10, EmsValue::Flammenstrom, EmsValue::None);
    parseNumeric(17, 1, 10, EmsValue::Systemdruck, EmsValue::None);

    if (canAccess(18, 2)) {
	std::ostringstream ss;
	ss << m_data[18] << m_data[19];
	m_valueHandler(EmsValue(EmsValue::ServiceCode, EmsValue::None, ss.str()));
    }
    if (canAccess(20, 2)) {
	std::ostringstream ss;
	ss << std::dec << (m_data[20] << 8 | m_data[21]);
	m_valueHandler(EmsValue(EmsValue::FehlerCode, EmsValue::None, ss.str()));
    }

    parseBool(7, 0, EmsValue::FlammeAktiv, EmsValue::None);
    parseBool(7, 2, EmsValue::BrennerAktiv, EmsValue::None);
    parseBool(7, 3, EmsValue::ZuendungAktiv, EmsValue::None);
    parseBool(7, 5, EmsValue::PumpeAktiv, EmsValue::Kessel);
    parseBool(7, 6, EmsValue::DreiWegeVentilAufWW, EmsValue::None);
    parseBool(7, 7, EmsValue::ZirkulationAktiv, EmsValue::None);
}

void
EmsMessage::parseUBAMonitorSlowMessage()
{
    parseNumeric(0, 2, 10, EmsValue::IstTemp, EmsValue::Aussen);
    parseNumeric(2, 2, 10, EmsValue::IstTemp, EmsValue::Kessel);
    parseNumeric(4, 2, 10, EmsValue::IstTemp, EmsValue::Abgas);
    parseNumeric(9, 1, 1, EmsValue::PumpenModulation, EmsValue::None);
    parseNumeric(10, 3, 1, EmsValue::Brennerstarts, EmsValue::None);
    parseNumeric(13, 3, 1, EmsValue::BetriebsZeit, EmsValue::None);
    parseNumeric(19, 3, 1, EmsValue::HeizZeit, EmsValue::None);
}

void
EmsMessage::parseUBAMonitorWWMessage()
{
    parseNumeric(0, 1, 1, EmsValue::SollTemp, EmsValue::WW);
    parseNumeric(1, 2, 10, EmsValue::IstTemp, EmsValue::WW);
    parseNumeric(10, 3, 1, EmsValue::WarmwasserbereitungsZeit, EmsValue::None);
    parseNumeric(13, 3, 1, EmsValue::WarmwasserBereitungen, EmsValue::None);

    parseBool(5, 0, EmsValue::Tagbetrieb, EmsValue::WW);
    parseBool(5, 1, EmsValue::EinmalLadungAktiv, EmsValue::WW);
    parseBool(5, 2, EmsValue::DesinfektionAktiv, EmsValue::WW);
    parseBool(5, 3, EmsValue::WarmwasserBereitung, EmsValue::None);
    parseBool(5, 4, EmsValue::NachladungAktiv, EmsValue::WW);
    parseBool(5, 5, EmsValue::WarmwasserTempOK, EmsValue::None);
    parseBool(7, 0, EmsValue::Tagbetrieb, EmsValue::Zirkulation);
    parseBool(7, 2, EmsValue::ZirkulationAktiv, EmsValue::None);

    if (canAccess(8, 1)) {
	EmsValue value(EmsValue::WWSystemType, EmsValue::None, m_data[8 - m_offset]);
	m_valueHandler(value);
    }
}

void
EmsMessage::parseUBAParameterWWMessage()
{
    if (canAccess(7, 1)) {
	EmsValue value(EmsValue::Schaltpunkte, EmsValue::Zirkulation, m_data[7 - m_offset]);
	m_valueHandler(value);
    }
}

void
EmsMessage::parseUBAErrorMessage()
{
    size_t start, bytes = m_data.size();

    if (m_offset % sizeof(EmsProto::ErrorRecord)) {
	start = ((m_offset / sizeof(EmsProto::ErrorRecord)) + 1) * sizeof(EmsProto::ErrorRecord);
    } else {
	start = m_offset;
    }

    while (canAccess(start, sizeof(EmsProto::ErrorRecord))) {
	EmsProto::ErrorRecord *record = (EmsProto::ErrorRecord *) &m_data.at(start - m_offset);
	unsigned int index = start / sizeof(EmsProto::ErrorRecord);
	EmsValue::ErrorEntry entry = { m_type, index, *record };

	m_valueHandler(EmsValue(EmsValue::Fehler, EmsValue::None, entry));
	start += sizeof(EmsProto::ErrorRecord);
    }
}

void
EmsMessage::parseUBAParametersMessage()
{
    parseNumeric(1, 1, 1, EmsValue::SetTemp, EmsValue::Kessel);
    parseNumeric(4, 1, 1, EmsValue::EinschaltHysterese, EmsValue::Kessel);
    parseNumeric(5, 1, 1, EmsValue::AusschaltHysterese, EmsValue::Kessel);
    parseNumeric(10, 1, 1, EmsValue::MinModulation, EmsValue::Kessel);
    parseNumeric(9, 1, 1, EmsValue::MaxModulation, EmsValue::Kessel);
    parseNumeric(6, 1, 1, EmsValue::AntipendelZeit, EmsValue::None);
    parseNumeric(8, 1, 1, EmsValue::PumpenNachlaufZeit, EmsValue::Kessel);
}

void
EmsMessage::parseRCTimeMessage()
{
    if (canAccess(0, sizeof(EmsProto::SystemTimeRecord))) {
	EmsProto::SystemTimeRecord *record = (EmsProto::SystemTimeRecord *) &m_data.at(0);
	EmsValue value(EmsValue::SystemZeit, EmsValue::None, *record);
	m_valueHandler(value);
    }
}

void
EmsMessage::parseRCOutdoorTempMessage()
{
    parseNumeric(0, 1, 1, EmsValue::GedaempfteTemp, EmsValue::Aussen);
}

void
EmsMessage::parseRCHKMonitorMessage(EmsValue::SubType subtype)
{
    parseNumeric(2, 1, 2, EmsValue::SollTemp, EmsValue::Raum);
    parseNumeric(3, 2, 10, EmsValue::IstTemp, EmsValue::Raum);

    if (canAccess(7, 3)) {
	EmsValue value(EmsValue::HKKennlinie, subtype, m_data[7 - m_offset],
		m_data[8 - m_offset], m_data[9 - m_offset]);
	m_valueHandler(value);
    }

    parseNumeric(14, 1, 1, EmsValue::SollTemp, subtype);
    parseNumeric(5, 1, 1, EmsValue::EinschaltoptimierungsZeit, subtype);
    parseNumeric(6, 1, 1, EmsValue::AusschaltoptimierungsZeit, subtype);

    if (canAccess(15, 1) && (m_data[15 - m_offset] & 1) == 0) {
	parseNumeric(10, 2, 100, EmsValue::TemperaturAenderung, EmsValue::Raum);
    }

    parseBool(0, 2, EmsValue::Automatikbetrieb, subtype);
    parseBool(0, 0, EmsValue::Ausschaltoptimierung, subtype);
    parseBool(0, 1, EmsValue::Einschaltoptimierung, subtype);
    parseBool(0, 3, EmsValue::WWVorrang, subtype);
    parseBool(0, 4, EmsValue::Estrichtrocknung, subtype);
    parseBool(0, 5, EmsValue::Ferien, subtype);
    parseBool(0, 6, EmsValue::Frostschutz, subtype);
    parseBool(1, 0, EmsValue::Sommerbetrieb, subtype);
    parseBool(1, 1, EmsValue::Tagbetrieb, subtype);
    parseBool(1, 7, EmsValue::Party, subtype);
    parseBool(13, 4, EmsValue::SchaltuhrEin, subtype);
}

void
EmsMessage::parseWMTemp1Message()
{
    parseNumeric(0, 2, 10, EmsValue::IstTemp, EmsValue::HK1);

    /* Byte 2 = 0 -> Pumpe aus, 100 = 0x64 -> Pumpe an */
    parseBool(2, 2, EmsValue::PumpeAktiv, EmsValue::HK1);
}

void
EmsMessage::parseWMTemp2Message()
{
    parseNumeric(0, 2, 10, EmsValue::IstTemp, EmsValue::HK1);
}

void
EmsMessage::parseMMTempMessage()
{
    parseNumeric(0, 1, 1, EmsValue::SollTemp, EmsValue::HK2);
    parseNumeric(1, 2, 10, EmsValue::IstTemp, EmsValue::HK2);
    parseNumeric(3, 1, 1, EmsValue::Mischersteuerung, EmsValue::None);

    /* Byte 3 = 0 -> Pumpe aus, 100 = 0x64 -> Pumpe an */
    parseBool(3, 2, EmsValue::PumpeAktiv, EmsValue::HK2);
}

