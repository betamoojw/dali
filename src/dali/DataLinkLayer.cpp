#include "Dali/DataLinkLayer.h"

#ifdef ARDUINO_ARCH_ESP32
#include "Dali/Receiver/Rmt.h"
#include "Dali/Transmitter/Rmt.h"
#endif
#ifdef ARDUINO_ARCH_RP2040
#include "Dali/Receiver/Pio.h"
#include "Dali/Transmitter/Pio.h"
#endif

namespace Dali
{
    bool DataLinkLayer::transmitFrame(Frame frame)
    {
        if (frame.size > 32) return false;

        _txQueue.push(frame);
        return true;
    };

    // This function actually does more than it should—it takes on tasks typically handled by the network layer.
    void DataLinkLayer::receivedFrame(Frame frame)
    {

        // check it response is in expeted time frame of 22TE.
        if (_nextResponse)
        {
            unsigned long _startReceiving = (frame.size + 1 + 2) * DALI_TE_TO_US(2);
            if (_startReceiving <= _nextResponseTimer)
            {
                frame.flags |= DALI_FRAME_BACKWARD;
                _nextResponse = false;
            }
            _nextResponse = false;
        }

        if (_txTransmitter->transmitting())
        {
            if (_txFrame.data == frame.data && _txFrame.size == frame.size)
            {
                frame.flags |= _txFrame.flags; // apply flags from txFrame
                frame.flags |= 0b00000100; // apply echo
                _nextResponse = true;          // wait for possible response
                _nextResponseTimer = micros() + DALI_TE_TO_US(22);
                // if (frame.flags & DALI_FRAME_FORWARD) _hack = true;
            }
            else
            {
                Serial.printf("Rx<%u>: %u: Frame collision %08X %08X\n", _txTransmitter->pin(), micros(), _txFrame.data, frame.data);
            }
        }

        // Regardless of the message received, the transmission is always terminated afterward.
        _txTransmitter->transmitting(false);

        _rxQueue.push(frame);
    };

    void DataLinkLayer::init(uint tx, uint rx)
    {
        // Initialize the transmitter and receiver based on the architecture.
#ifdef ARDUINO_ARCH_ESP32
        _rxReceiver = new Dali::Receiver::Rmt(this, rx);
        _txTransmitter = new Dali::Transmitter::Rmt(this, tx);
#endif
#ifdef ARDUINO_ARCH_RP2040
        _rxReceiver = new Dali::Receiver::Pio(this, rx);
        _txTransmitter = new Dali::Transmitter::Pio(this, tx);
#endif
    };

    void DataLinkLayer::process()
    {
        _rxReceiver->process();
        processReceive();
        _rxReceiver->process();
        processReceive();
        processTransmit();
        _txTransmitter->process();
    };

    void DataLinkLayer::processReceive()
    {
        if (_rxQueue.empty()) return;

        Frame rxFrame = _rxQueue.front();
        _rxQueue.pop();

        Serial.printf("Rx<%u>: %u: Frame 0x%08X (S: %u - F: %u)\n", _rxReceiver->pin(), micros(), rxFrame.data, rxFrame.size, rxFrame.flags);

        for (std::function<void(Frame)> &callback : _callbackMonitors)
            callback(rxFrame);

        if (_txFrame.flags & DALI_FRAME_FORWARD && rxFrame.flags & DALI_FRAME_BACKWARD && rxFrame.flags != DALI_FRAME_ERROR)
            _callbackResponse(_txFrame, rxFrame);
    }

    void DataLinkLayer::processTransmit()
    {
        if (_txQueue.empty()) return;

        if (_txTransmitter->transmitting()) return;
        if (_rxReceiver->receiving()) return;

        Frame txFrame = _txQueue.front();
        const unsigned long diff = (micros())-_rxReceiver->lastReceiving() - 2;
        if (txFrame.flags & DALI_FRAME_BACKWARD)
        {
            // backward frames should send after 7TE
            if (diff < DALI_TE_TO_US(7)) return;
            // Serial.printf("%u: Check Backward ok (%u)\n", _txTransmitter->pin(), diff);
        }
        else
        {
            // forward frames should send after 22TE to allow receiving an answer
            if (diff < DALI_TE_TO_US(22)) return;
            // Serial.printf("%u: Check Forward ok (%u)\n", _txTransmitter->pin(), diff);
        }
        _txFrame = txFrame;
        _txQueue.pop();

        Serial.printf("Tx<%u>: %u: Frame 0x%08X (S: %u - F: %u)\n", _txTransmitter->pin(), micros(), _txFrame.data, _txFrame.size, _txFrame.flags);
        _txTransmitter->transmitFrame(_txFrame);
        _nextResponse = false;
    }

    void DataLinkLayer::registerMonitor(std::function<void(Frame)> callback)
    {
        _callbackMonitors.push_back(callback);
    }

    void DataLinkLayer::registerResponse(std::function<void(Frame, Frame)> callback)
    {
        _callbackResponse = callback;
    }

    bool DataLinkLayer::receiving()
    {
        return _rxReceiver->receiving();
    }
}; // namespace Dali
