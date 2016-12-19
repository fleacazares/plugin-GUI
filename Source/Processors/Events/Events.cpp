/*
------------------------------------------------------------------

This file is part of the Open Ephys GUI
Copyright (C) 2014 Open Ephys

------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "Events.h"
#include "../GenericProcessor/GenericProcessor.h"
//EventBase

EventType EventBase::getBaseType() const
{
	return m_baseType;
}

EventType EventBase::getBaseType(const MidiMessage& msg)
{
	const uint8* data = msg.getRawData();
	return static_cast<EventType>(*data);
}

EventBase::EventBase(EventType type, uint64 timestamp)
	: m_baseType(type), m_timestamp(timestamp)
{}

EventBase* EventBase::deserializeFromMessage(const MidiMessage& msg, const GenericProcessor* processor)
{
	if (!processor) return nullptr;

	const uint8* data = msg.getRawData();
	const EventType type = static_cast<EventType>(*data);
	const uint16 processorID = static_cast<uint16>(*(data + 2));
	const uint16 subProcessorID = static_cast<uint16>(*(data + 4));
	const uint16 channelIDX = static_cast<uint16>(*(data + 6));

	switch (type)
	{
	case PROCESSOR_EVENT:
	{		const EventChannel* chan = processor->getEventChannel(processor->getEventChannelIndex(channelIDX, processorID, subProcessorID));
			return Event::deserializeFromMessage(msg, chan);
	}
	case SPIKE_EVENT:
	{
		const SpikeChannel* chan = processor->getSpikeChannel(processor->getSpikeChannelIndex(channelIDX, processorID, subProcessorID));
		return SpikeEvent::deserializeFromMessage(msg, chan);
	}
	default:
		return nullptr;
		break;
	}
}

bool EventBase::compareMetaData(const MetaDataEventObject* channelInfo, const MetaDataValueArray& metaData)
{
	int metaDataSize = metaData.size();

	if (metaDataSize != channelInfo->getEventMetaDataCount()) return false;

	for (int i = 0; i < metaDataSize; i++)
	{
		if (!metaData[i]->isOfType(channelInfo->getEventMetaDataDescriptor(i))) return false;
	}
	return true;
}

uint16 EventBase::getSourceID(const MidiMessage& msg)
{
	const uint8* data = msg.getRawData();
	return static_cast<uint16>(*data + 2);
}

uint16 EventBase::getSubProcessorIdx(const MidiMessage& msg)
{
	const uint8* data = msg.getRawData();
	return static_cast<uint16>(*data + 4);
}

uint16 EventBase::getSourceIndex(const MidiMessage& msg)
{
	const uint8* data = msg.getRawData();
	return static_cast<uint16>(*data + 4);
}

//Event
EventChannel::EventChannelTypes Event::getEventType() const
{
	return m_eventType;
}

const EventChannel* Event::getChannelInfo() const
{
	return m_channelInfo;
}

EventChannel::EventChannelTypes Event::getEventType(const MidiMessage& msg)
{
	const uint8* data = msg.getRawData();
	return static_cast<EventChannel::EventChannelTypes>(*(data + 1));
}

Event::Event(const EventChannel* channelInfo, uint64 timestamp, uint16 channel)
	: EventBase(PROCESSOR_EVENT, timestamp),
	m_channel(channel),
	m_channelInfo(channelInfo),
	m_eventType(channelInfo->getChannelType())
{}

Event* Event::deserializeFromMessage(const MidiMessage& msg, const EventChannel* channelInfo)
{
	EventChannel::EventChannelTypes type = channelInfo->getChannelType();
	
	if (type == EventChannel::TTL)
		return TTLEvent::deserializeFromMessage(msg, channelInfo);
	else if (type == EventChannel::TEXT)
		return TextEvent::deserializeFromMessage(msg, channelInfo);
	else if (type >= EventChannel::INT8_ARRAY && type <= EventChannel::DOUBLE_ARRAY)
		return BinaryEvent::deserializeFromMessage(msg, channelInfo);
	else return nullptr;
}

uint16 Event::getChannel() const
{
	return m_channel;
}

bool Event::serializeHeader(EventChannel::EventChannelTypes type, char* buffer, size_t dstSize) const
{
	size_t dataSize = m_channelInfo->getDataSize();
	size_t eventSize = dataSize + EVENT_BASE_SIZE;
	size_t totalSize = eventSize + m_channelInfo->getTotalEventMetaDataSize();
	if (totalSize < dstSize)
	{
		jassertfalse;
		return false;
	}

	*(buffer + 0) = PROCESSOR_EVENT;
	*(buffer + 1) = static_cast<char>(type);
	*(reinterpret_cast<uint16*>(buffer + 2)) = m_channelInfo->getSourceNodeID();
	*(reinterpret_cast<uint16*>(buffer + 4)) = m_channelInfo->getSubProcessorIdx();
	*(reinterpret_cast<uint16*>(buffer + 6)) = m_channelInfo->getSourceIndex();
	*(reinterpret_cast<uint64*>(buffer + 8)) = m_timestamp;
	*(reinterpret_cast<uint64*>(buffer + 16)) = m_channel;
	return true;
}

bool Event::createChecks(const EventChannel* channelInfo, EventChannel::EventChannelTypes eventType, uint16 channel)
{
	if (!channelInfo) return false;
	if (channelInfo->getChannelType() != EventChannel::TTL) return false;
	if ((channel < 0) || (channel >= channelInfo->getNumChannels())) return false;
	if (channelInfo->getEventMetaDataCount() != 0) return false;
	return true;
}

bool Event::createChecks(const EventChannel* channelInfo, EventChannel::EventChannelTypes eventType, uint16 channel, const MetaDataValueArray& metaData)
{
	if (!channelInfo) return false;
	if (channelInfo->getChannelType() != EventChannel::TTL) return false;
	if ((channel < 0) || (channel >= channelInfo->getNumChannels())) return false;
	if (!compareMetaData(channelInfo, metaData)) return nullptr;
	return true;
}

//TTLEvent

TTLEvent::TTLEvent(const EventChannel* channelInfo, uint64 timestamp, uint16 channel, const void* eventData)
	: Event(channelInfo, timestamp, channel)
{
	size_t size = m_channelInfo->getDataSize();
	m_data.malloc(size);
	memcpy(m_data.getData(), eventData, size);
}

bool TTLEvent::getState() const
{
	int byteIndex = m_channel / 8;
	int bitIndex = m_channel % 8;

	char data = m_data[byteIndex];
	return ((1 << bitIndex) & data);
}

const void* TTLEvent::getTTLWordPointer() const
{
	return m_data.getData();
}

void TTLEvent::serialize(void* dstBuffer, size_t dstSize) const
{
	char* buffer = static_cast<char*>(dstBuffer);
	if (!serializeHeader(EventChannel::TTL, buffer, dstSize))
		return;
	
	size_t dataSize = m_channelInfo->getDataSize();
	size_t eventSize = dataSize + EVENT_BASE_SIZE;
	memcpy((buffer + EVENT_BASE_SIZE), m_data.getData(), dataSize);
	serializeMetaData(buffer + eventSize);
}

TTLEvent* TTLEvent::createTTLEvent(const EventChannel* channelInfo, uint64 timestamp, const void* eventData, int dataSize, uint16 channel)
{

	if (!createChecks(channelInfo, EventChannel::TTL, channel))
	{
		jassertfalse;
		return nullptr;
	}
	
	if (dataSize < channelInfo->getDataSize()) 
	{
		jassertfalse;
		return nullptr;
	}
	
	return new TTLEvent(channelInfo, timestamp, channel, eventData);
}

TTLEvent* TTLEvent::createTTLEvent(const EventChannel* channelInfo, uint64 timestamp, const void* eventData, int dataSize, const MetaDataValueArray& metaData, uint16 channel)
{

	if (!createChecks(channelInfo, EventChannel::TTL, channel, metaData))
	{
		jassertfalse;
		return nullptr;
	}

	if (dataSize < channelInfo->getDataSize())
	{
		jassertfalse;
		return nullptr;
	}

	TTLEvent* event = new TTLEvent(channelInfo, timestamp, channel, eventData);
	
	event->m_metaDataValues.addArray(metaData);
	return event;
}

TTLEvent* TTLEvent::deserializeFromMessage(const MidiMessage& msg, const EventChannel* channelInfo)
{
	size_t totalSize = msg.getRawDataSize();
	size_t dataSize = channelInfo->getDataSize();
	size_t metaDataSize = channelInfo->getTotalEventMetaDataSize();

	if (totalSize != (dataSize + EVENT_BASE_SIZE + metaDataSize))
	{
		jassertfalse;
		return nullptr;
	}
	const uint8* buffer = msg.getRawData();
	if ((buffer + 0) != PROCESSOR_EVENT)
	{
		jassertfalse;
		return nullptr;
	}

	if (channelInfo->getChannelType() != EventChannel::TTL)
	{
		jassertfalse;
		return nullptr;
	}

	if ((buffer + 1) != EventChannel::TTL) {
		jassertfalse;
		return nullptr;
	}

	uint64 timestamp = *(reinterpret_cast<const uint64*>(buffer + 8));
	uint16 channel = *(reinterpret_cast<const uint16*>(buffer + 16));

	ScopedPointer<TTLEvent> event = new TTLEvent(channelInfo, timestamp, channel, (buffer + EVENT_BASE_SIZE));
	bool ret;
	if (metaDataSize > 0)
		 ret = event->deserializeMetaData(channelInfo, (buffer + EVENT_BASE_SIZE + dataSize), metaDataSize);

	if (ret)
		return event.release();
	else
	{
		jassertfalse;
		return nullptr;
	}
}

//TextEvent
TextEvent::TextEvent(const EventChannel* channelInfo, uint64 timestamp, uint16 channel, const String& text)
	: Event(channelInfo, timestamp, channel),
	m_text(text)
{
}

String TextEvent::getText() const
{
	return m_text;
}

void TextEvent::serialize(void* dstBuffer, size_t dstSize) const
{
	char* buffer = static_cast<char*>(dstBuffer);
	if (!serializeHeader(EventChannel::TEXT, buffer, dstSize))
		return;

	size_t dataSize = m_channelInfo->getDataSize();
	size_t eventSize = dataSize + EVENT_BASE_SIZE;
	size_t stringSize = m_text.getNumBytesAsUTF8();
	memcpy((buffer + EVENT_BASE_SIZE), m_text.toUTF8(), stringSize);
	if ((dataSize - stringSize) > 0)
		zeromem((buffer + EVENT_BASE_SIZE + stringSize), dataSize - stringSize);
	serializeMetaData(buffer + eventSize);
}

TextEvent* TextEvent::createTextEvent(const EventChannel* channelInfo, uint64 timestamp, const String& text, uint16 channel)
{
	if (!createChecks(channelInfo, EventChannel::TEXT, channel))
	{
		jassertfalse;
		return nullptr;
	}

	if (text.length() > channelInfo->getLength())
	{
		jassertfalse;
		return nullptr;
	}

	return new TextEvent(channelInfo, timestamp, channel, text);
}

TextEvent* TextEvent::createTextEvent(const EventChannel* channelInfo, uint64 timestamp, const String& text, const MetaDataValueArray& metaData, uint16 channel)
{
	if (!createChecks(channelInfo, EventChannel::TEXT, channel, metaData))
	{
		jassertfalse;
		return nullptr;
	}

	if (text.length() > channelInfo->getLength())
	{
		jassertfalse;
		return nullptr;
	}

	TextEvent* event = new TextEvent(channelInfo, timestamp, channel, text);
	
	event->m_metaDataValues.addArray(metaData);
	return event;
}

TextEvent* TextEvent::deserializeFromMessage(const MidiMessage& msg, const EventChannel* channelInfo)
{
	size_t totalSize = msg.getRawDataSize();
	size_t dataSize = channelInfo->getDataSize();
	size_t metaDataSize = channelInfo->getTotalEventMetaDataSize();

	if (totalSize != (dataSize + EVENT_BASE_SIZE + metaDataSize))
	{
		jassertfalse;
		return nullptr;
	}
	const uint8* buffer = msg.getRawData();
	if ((buffer + 0) != PROCESSOR_EVENT)
	{
		jassertfalse;
		return nullptr;
	}

	if (channelInfo->getChannelType() != EventChannel::TEXT)
	{
		jassertfalse;
		return nullptr;
	}

	if ((buffer + 1) != EventChannel::TEXT) {
		jassertfalse;
		return nullptr;
	}

	uint64 timestamp = *(reinterpret_cast<const uint64*>(buffer + 8));
	uint16 channel = *(reinterpret_cast<const uint16*>(buffer + 16));
	String text = String(CharPointer_UTF8(reinterpret_cast<const char*>(buffer + EVENT_BASE_SIZE)), dataSize);

	ScopedPointer<TextEvent> event = new TextEvent(channelInfo, timestamp, channel, text);
	bool ret;
	if (metaDataSize > 0)
		ret = event->deserializeMetaData(channelInfo, (buffer + EVENT_BASE_SIZE + dataSize), metaDataSize);

	if (ret)
		return event.release();
	else
	{
		jassertfalse;
		return nullptr;
	}
}

//BinaryEvent
BinaryEvent::BinaryEvent(const EventChannel* channelInfo, uint64 timestamp, uint16 channel, const void* data, EventChannel::EventChannelTypes type)
	: Event(channelInfo, timestamp, channel),
	m_type(type)
{
	size_t size = m_channelInfo->getDataSize();
	m_data.malloc(size);
	memcpy(m_data.getData(), data, size);
}

const void* BinaryEvent::getBinaryDataPointer() const
{
	return m_data.getData();
}

EventChannel::EventChannelTypes BinaryEvent::getBinaryType() const
{
	return m_type;
}

template<typename T>
EventChannel::EventChannelTypes BinaryEvent::getType()
{
	if (std::is_same<int8, T>::value) return EventChannel::INT8_ARRAY;
	if (std::is_same<uint8, T>::value) return EventChannel::UINT8_ARRAY;
	if (std::is_same<int16, T>::value) return EventChannel::INT16_ARRAY;
	if (std::is_same<uint16, T>::value) return EventChannel::UINT16_ARRAY;
	if (std::is_same<int32, T>::value) return EventChannel::INT32_ARRAY;
	if (std::is_same<uint32, T>::value) return EventChannel::UINT32_ARRAY;
	if (std::is_same<int64, T>::value) return EventChannel::INT64_ARRAY;
	if (std::is_same<uint64, T>::value) return EventChannel::UINT64_ARRAY;
	if (std::is_same<float, T>::value) return EventChannel::FLOAT_ARRAY;
	if (std::is_same<double, T>::value) return EventChannel::DOUBLE_ARRAY;

	return EventChannel::INVALID;
}

void BinaryEvent::serialize(void* dstBuffer, size_t dstSize) const
{
	char* buffer = static_cast<char*>(dstBuffer);
	if (!serializeHeader(m_type, buffer, dstSize))
		return;
	
	size_t dataSize = m_channelInfo->getDataSize();
	size_t eventSize = dataSize + EVENT_BASE_SIZE;
	memcpy((buffer + EVENT_BASE_SIZE), m_data.getData(), dataSize);
	serializeMetaData(buffer + eventSize);
}

template<typename T>
BinaryEvent* BinaryEvent::createBinaryEvent(const EventChannel* channelInfo, uint64 timestamp, const T* data, int dataSize, uint16 channel)
{
	EventChannel::EventChannelTypes type = getType<T>();
	if (type == EventChannel::INVALID)
	{
		jassertfalse;
		return nullptr;
	}

	if (!createChecks(channelInfo, type, channel))
	{
		jassertfalse;
		return nullptr;
	}

	if (dataSize < channelInfo->getDataSize())
	{
		jassertfalse;
		return nullptr;
	}

	return new BinaryEvent(channelInfo, timestamp, channel, data, type);
}

template<typename T>
BinaryEvent* BinaryEvent::createBinaryEvent(const EventChannel* channelInfo, uint64 timestamp, const T* data, int dataSize, const MetaDataValueArray& metaData, uint16 channel)
{
	EventChannel::EventChannelTypes type = getType<T>();
	if (type == EventChannel::INVALID)
	{
		jassertfalse;
		return nullptr;
	}

	if (!createChecks(channelInfo, EventChannel::TTL, channel, metaData))
	{
		jassertfalse;
		return nullptr;
	}

	if (dataSize < channelInfo->getDataSize())
	{
		jassertfalse;
		return nullptr;
	}

	BinaryEvent* event = new BinaryEvent(channelInfo, timestamp, channel, data, type);
	event->m_metaDataValues.addArray(metaData);
	return event;
}

BinaryEvent* BinaryEvent::deserializeFromMessage(const MidiMessage& msg, const EventChannel* channelInfo)
{
	size_t totalSize = msg.getRawDataSize();
	size_t dataSize = channelInfo->getDataSize();
	size_t metaDataSize = channelInfo->getTotalEventMetaDataSize();

	if (totalSize != (dataSize + EVENT_BASE_SIZE + metaDataSize))
	{
		jassertfalse;
		return nullptr;
	}
	const uint8* buffer = msg.getRawData();
	if ((buffer + 0) != PROCESSOR_EVENT)
	{
		jassertfalse;
		return nullptr;
	}

	if (channelInfo->getChannelType() < EventChannel::BINARY_BASE_VALUE || channelInfo->getChannelType() >= EventChannel::INVALID)
	{
		jassertfalse;
		return nullptr;
	}

	const EventChannel::EventChannelTypes type = static_cast<const EventChannel::EventChannelTypes>(*(buffer + 1));
	if (type != channelInfo->getChannelType())
	{
		jassertfalse;
		return nullptr;
	}

	uint64 timestamp = *(reinterpret_cast<const uint64*>(buffer + 8));
	uint16 channel = *(reinterpret_cast<const uint16*>(buffer + 16));

	ScopedPointer<BinaryEvent> event = new BinaryEvent(channelInfo, timestamp, channel, (buffer + EVENT_BASE_SIZE), type);
	bool ret;
	if (metaDataSize > 0)
		ret = event->deserializeMetaData(channelInfo, (buffer + EVENT_BASE_SIZE + dataSize), metaDataSize);

	if (ret)
		return event.release();
	else
	{
		jassertfalse;
		return nullptr;
	}
}

//SpikeEvent
SpikeEvent::SpikeEvent(const SpikeChannel* channelInfo, uint64 timestamp, Array<float> thresholds, HeapBlock<float>& data)
	: EventBase(SPIKE_EVENT, timestamp),
	m_thresholds(thresholds),
	m_channelInfo(channelInfo)
{
	m_data.swapWith(data);
}

const float* SpikeEvent::getDataPointer() const
{
	return m_data.getData();
}

const float* SpikeEvent::getDataPointer(int channel) const
{
	if ((channel < 0) || (channel >= m_channelInfo->getNumChannels()))
	{
		jassertfalse;
		return nullptr;
	}
	return (m_data.getData() + (channel*m_channelInfo->getTotalSamples()));
}

float SpikeEvent::getThreshold(int chan) const
{
	return m_thresholds[chan];
}

void SpikeEvent::serialize(void* dstBuffer, size_t dstSize) const
{
	size_t dataSize = m_channelInfo->getDataSize();
	size_t eventSize = dataSize + SPIKE_BASE_SIZE + m_thresholds.size() * sizeof(float);
	size_t totalSize = eventSize + m_channelInfo->getTotalEventMetaDataSize();
	if (totalSize < dstSize)
	{
		jassertfalse;
		return;
	}

	char* buffer = static_cast<char*>(dstBuffer);

	*(buffer + 0) = SPIKE_EVENT;
	*(buffer + 1) = static_cast<char>(m_channelInfo->getChannelType());
	*(reinterpret_cast<uint16*>(buffer + 2)) = m_channelInfo->getSourceNodeID();
	*(reinterpret_cast<uint16*>(buffer + 4)) = m_channelInfo->getSubProcessorIdx();
	*(reinterpret_cast<uint16*>(buffer + 6)) = m_channelInfo->getSourceIndex();
	*(reinterpret_cast<uint64*>(buffer + 8)) = m_timestamp;
	int memIdx = SPIKE_BASE_SIZE;
	for (int i = 0; i < m_thresholds.size(); i++)
	{
		*(reinterpret_cast<float*>(buffer + memIdx)) = m_thresholds[i];
		memIdx += sizeof(float);
	}
	memcpy((buffer + memIdx), m_data.getData(), dataSize);
	serializeMetaData(buffer + eventSize);
}

SpikeEvent* SpikeEvent::createBasicSpike(const SpikeChannel* channelInfo, uint64 timestamp, Array<float> thresholds, SpikeBuffer& dataSource)
{
	if (!dataSource.m_ready)
	{
		jassertfalse;
		return nullptr;
	}
	int nChannels = channelInfo->getNumChannels();
	if (nChannels != dataSource.m_nChans)
	{
		jassertfalse;
		return nullptr;
	}
	int nSamples = channelInfo->getTotalSamples();
	if (nSamples != dataSource.m_nSamps)
	{
		jassertfalse;
		return nullptr;
	}
	if (thresholds.size() != nChannels)
	{
		jassertfalse;
		return nullptr;
	}
	dataSource.m_ready = false;
	return new SpikeEvent(channelInfo, timestamp, thresholds, dataSource.m_data);

}

SpikeEvent* SpikeEvent::createSpikeEvent(const SpikeChannel* channelInfo, uint64 timestamp, Array<float> thresholds, SpikeBuffer& dataSource)
{
	if (!channelInfo)
	{
		jassertfalse;
		return nullptr;
	}

	
	if (channelInfo->getEventMetaDataCount() != 0)
	{
		jassertfalse;
		return nullptr;
	}

	return createBasicSpike(channelInfo, timestamp, thresholds, dataSource);	
	
}

SpikeEvent* SpikeEvent::createSpikeEvent(const SpikeChannel* channelInfo, uint64 timestamp, Array<float> thresholds, SpikeBuffer& dataSource, const MetaDataValueArray& metaData)
{
	if (!channelInfo)
	{
		jassertfalse;
		return nullptr;
	}

	if (!compareMetaData(channelInfo, metaData))
	{
		jassertfalse;
		return nullptr;
	}
	SpikeEvent* event = createBasicSpike(channelInfo, timestamp, thresholds, dataSource);
	if (!event)
	{
		jassertfalse;
		return nullptr;
	}

	event->m_metaDataValues.addArray(metaData);
	return event;
}

SpikeEvent* SpikeEvent::deserializeFromMessage(const MidiMessage& msg, const SpikeChannel* channelInfo)
{
	int nChans = channelInfo->getNumChannels();
	size_t totalSize = msg.getRawDataSize();
	size_t dataSize = channelInfo->getDataSize();
	size_t thresholdSize = nChans*sizeof(float);
	size_t metaDataSize = channelInfo->getTotalEventMetaDataSize();

	if (totalSize != (thresholdSize + dataSize + SPIKE_BASE_SIZE + metaDataSize))
	{
		jassertfalse;
		return nullptr;
	}
	const uint8* buffer = msg.getRawData();
	if ((buffer + 0) != SPIKE_EVENT)
	{
		jassertfalse;
		return nullptr;
	}

	if ((buffer + 1) != channelInfo->getChannelType())
	{
		jassertfalse;
		return nullptr;
	}
	uint64 timestamp = *(reinterpret_cast<const uint64*>(buffer + 8));
	Array<float> thresholds;
	thresholds.addArray(reinterpret_cast<const float*>(buffer + SPIKE_BASE_SIZE), nChans);
	HeapBlock<float> data;
	data.malloc(dataSize, sizeof(char));
	memcpy(data.getData(), (buffer + SPIKE_BASE_SIZE + thresholdSize), dataSize);

	ScopedPointer<SpikeEvent> event = new SpikeEvent(channelInfo, timestamp, thresholds, data);

	bool ret;
	if (metaDataSize > 0)
		ret = event->deserializeMetaData(channelInfo, (buffer + SPIKE_BASE_SIZE + dataSize + thresholdSize), metaDataSize);

	if (ret)
		return event.release();
	else
	{
		jassertfalse;
		return nullptr;
	}
}

SpikeEvent::SpikeBuffer::SpikeBuffer(const SpikeChannel* channelInfo)
	: m_nChans(channelInfo->getNumChannels()),
	m_nSamps(channelInfo->getTotalSamples())
{
	m_data.malloc(m_nChans*m_nSamps);
}

float* SpikeEvent::SpikeBuffer::operator[](const int index)
{
	if (!m_ready)
	{
		jassertfalse;
		return nullptr;
	}
	if (index < 0 || index >= m_nChans)
	{
		jassertfalse;
		return nullptr;
	}
	return m_data.getData() + (index + m_nSamps);
}

//Template definitions
template BinaryEvent* BinaryEvent::createBinaryEvent<int8>(const EventChannel*, uint64, const int8* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint8>(const EventChannel*, uint64, const uint8* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<int16>(const EventChannel*, uint64, const int16* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint16>(const EventChannel*, uint64, const uint16* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<int32>(const EventChannel*, uint64, const int32* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint32>(const EventChannel*, uint64, const uint32* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<int64>(const EventChannel*, uint64, const int64* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint64>(const EventChannel*, uint64, const uint64* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<float>(const EventChannel*, uint64, const float* data, int, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<double>(const EventChannel*, uint64, const double* data, int, uint16);

template BinaryEvent* BinaryEvent::createBinaryEvent<int8>(const EventChannel*, uint64, const int8* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint8>(const EventChannel*, uint64, const uint8* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<int16>(const EventChannel*, uint64, const int16* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint16>(const EventChannel*, uint64, const uint16* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<int32>(const EventChannel*, uint64, const int32* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint32>(const EventChannel*, uint64, const uint32* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<int64>(const EventChannel*, uint64, const int64* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<uint64>(const EventChannel*, uint64, const uint64* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<float>(const EventChannel*, uint64, const float* data, int, const MetaDataValueArray&, uint16);
template BinaryEvent* BinaryEvent::createBinaryEvent<double>(const EventChannel*, uint64, const double* data, int, const MetaDataValueArray&, uint16);