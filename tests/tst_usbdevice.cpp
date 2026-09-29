// Native USB backend (core/src/usb) against libmydaq's mock transport: the UsbDevice contract,
// error mapping and the provider's hot-plug logic, without hardware.
#include "usb/usb_ai_task.h"
#include "usb/usb_device.h"
#include "usb/usb_provider.h"

#include <mock/mydaq_mock.h>

#include <QTest>

#include <memory>

using namespace mydaq;

namespace {

const char* const kCalmem = MYDAQ_TEST_DATA_DIR "/calmem-036CD1C6.bin";

// An emulated myDAQ opened through the whole library stack.
struct Emulated {
    Emulated()
    {
        context = usb::makeContext(false);
        mock = mydaq_mock_new_emulator_file(kCalmem);
        if (!mock)
            throw std::runtime_error("calibration dump missing");
        mydaq_device* dev = nullptr;
        usb::check(mydaq_mock_open(context.get(), mock, 0, &dev), QStringLiteral("mock open"));
        device = std::make_shared<usb::UsbDevice>(context, dev);
    }
    ~Emulated()
    {
        device.reset();   // closes the libmydaq device before its transport goes away
        mydaq_mock_free(mock);
    }
    usb::ContextPtr context;
    mydaq_mock* mock = nullptr;
    std::shared_ptr<usb::UsbDevice> device;
};

template <typename F>
DeviceError::Code errorOf(F&& f)
{
    try {
        f();
    } catch (const DeviceError& e) {
        return e.code();
    }
    return DeviceError::Code(-1);
}

// Bus with emulated devices that tests plug and unplug.
class FakeBus final : public usb::Bus {
public:
    struct Slot {
        std::string location;
        uint8_t address;
        std::string serial;
        bool busy = false;
    };

    ~FakeBus() override
    {
        for (mydaq_mock* m : mocks)
            mydaq_mock_free(m);
    }
    std::vector<Attached> attached() override
    {
        std::vector<Attached> out;
        for (const Slot& s : ports)
            out.push_back({s.location, s.address});
        return out;
    }
    std::shared_ptr<usb::UsbDevice> open(const std::string& location) override
    {
        ++opens;
        for (const Slot& s : ports) {
            if (s.location != location)
                continue;
            if (s.busy)
                throw DeviceError(DeviceError::ResourceBusy, QStringLiteral("in use"));
            mydaq_mock* m = mydaq_mock_new_emulator_file(kCalmem);
            mocks.push_back(m);
            mydaq_mock_set_serial(m, s.serial.c_str(), s.location.c_str());
            mydaq_mock_set_address(m, s.address);
            mydaq_device* dev = nullptr;
            usb::check(mydaq_mock_open(context.get(), m, 0, &dev), QStringLiteral("mock open"));
            return std::make_shared<usb::UsbDevice>(context, dev);
        }
        throw DeviceError(DeviceError::NotConnected, QStringLiteral("gone"));
    }

    usb::ContextPtr context = usb::makeContext(false);
    std::vector<Slot> ports;
    std::vector<mydaq_mock*> mocks;   // freed after the provider released every device
    int opens = 0;
};

usb::UsbProvider::Options manualOptions()
{
    usb::UsbProvider::Options o;
    o.settleTime = std::chrono::milliseconds(0);
    o.retryBusy = std::chrono::milliseconds(0);
    o.retryError = std::chrono::milliseconds(0);
    o.startThread = false;
    return o;
}

}  // namespace

class TestUsbDevice : public QObject {
    Q_OBJECT

private slots:
    void info()
    {
        Emulated e;
        const DeviceInfo info = e.device->info();
        QCOMPARE(info.name, QStringLiteral("myDAQ (036CD1C6)"));
        QCOMPARE(info.id, QStringLiteral("usb:036CD1C6"));
        QCOMPARE(info.productType, QStringLiteral("NI myDAQ"));
        QCOMPARE(info.serialNumber, QStringLiteral("036CD1C6"));
        QVERIFY(!info.simulated);
        QVERIFY(e.device->isConnected());
        QCOMPARE(e.device->maxAiRate(), 200e3);
        QCOMPARE(e.device->coerceAiRate(3000.0), 1e8 / 33333.0);
        QCOMPARE(e.device->coerceAiRate(1e9), 200e3);
    }

    void staticAi()
    {
        Emulated e;
        const mydaq_calibration* cal = mydaq_get_calibration(e.device->handle());
        const double v = e.device->aiReadStatic(AiChannel::AI0, 10.0);
        QCOMPARE(v, mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_10V, -8113 - 2));
        QCOMPARE(errorOf([&] { e.device->aiReadStatic(AiChannel::AI1, 5.0); }), DeviceError::InvalidConfig);
        QCOMPARE(errorOf([&] { e.device->aiReadStatic(AiChannel::AudioInLeft, 2.0); }), DeviceError::NotSupported);
    }

    void finiteAiTask()
    {
        Emulated e;
        AiConfig config;
        config.channels = {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 2.0}};
        config.sampleRate = 44100.0;
        config.samplesPerChannel = 3000;
        config.mode = SampleMode::Finite;
        auto task = e.device->createAiTask(config);
        QCOMPARE(task->sampleRate(), 1e8 / 2267.0);
        QVERIFY(task->channelSkew() > 0.0);
        // AI is reserved while the task exists
        QCOMPARE(errorOf([&] { e.device->aiReadStatic(AiChannel::AI0, 10.0); }), DeviceError::ResourceBusy);
        QCOMPARE(errorOf([&] { e.device->createAiTask(config); }), DeviceError::ResourceBusy);

        const mydaq_calibration* cal = mydaq_get_calibration(e.device->handle());
        for (int round = 0; round < 2; ++round) {
            task->start();
            auto first = task->read(1000, 2000);
            QCOMPARE(first.size(), size_t(2));
            QCOMPARE(first[0].size(), size_t(1000));
            QCOMPARE(first[0][0], mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_10V, -8113 - 2));
            QCOMPARE(first[1][1], mydaq_ai_code_to_volts(cal, MYDAQ_AI_RANGE_2V, 3000 - 1));
            QCOMPARE(errorOf([&] { task->read(2001, 100); }), DeviceError::InvalidConfig);
            auto rest = task->read(2000, 2000);
            QCOMPARE(rest[1].size(), size_t(2000));
            QVERIFY(!task->isRunning());
            QCOMPARE(task->read(-1, 0)[0].size(), size_t(0));
        }
        task.reset();
        QVERIFY(std::isfinite(e.device->aiReadStatic(AiChannel::AI0, 10.0)));
    }

    void aiReadTimeoutAndStop()
    {
        Emulated e;
        mydaq_mock_set_realtime(e.mock, 1);
        AiConfig config;
        config.channels = {{AiChannel::AI0, 10.0}};
        config.sampleRate = 1000.0;
        config.samplesPerChannel = 2000;
        auto task = e.device->createAiTask(config);
        task->start();
        QVERIFY(task->isRunning());
        QCOMPARE(errorOf([&] { task->read(2000, 30); }), DeviceError::Timeout);
        QVERIFY(task->available() < 2000);
        task->stop();
        task->stop();   // idempotent
        QVERIFY(!task->isRunning());
    }

    void continuousAiTask()
    {
        Emulated e;
        mydaq_mock_set_realtime(e.mock, 1);
        AiConfig config;
        config.channels = {{AiChannel::AI0, 10.0}, {AiChannel::AI1, 10.0}};
        config.sampleRate = 20e3;
        config.samplesPerChannel = 1000;   // buffer hint
        config.mode = SampleMode::Continuous;
        auto task = e.device->createAiTask(config);
        task->start();
        for (int k = 0; k < 5; ++k) {
            auto block = task->read(1000, 2000);
            QCOMPARE(block.size(), size_t(2));
            QCOMPARE(block[1].size(), size_t(1000));
        }
        QVERIFY(task->isRunning());
        QVERIFY(task->read(-1, 0).front().size() < 1000);   // non-blocking: whatever is there
        task->stop();
        QVERIFY(!task->isRunning());
        QVERIFY(mydaq_mock_oneway_frames(e.mock) >= 2);   // read-position reports
    }

    void audioIn()
    {
        Emulated e;
        AiConfig config;
        config.channels = {{AiChannel::AudioInLeft, 2.0}, {AiChannel::AudioInRight, 2.0}};
        config.sampleRate = 48000.0;
        config.samplesPerChannel = 480;
        auto task = e.device->createAiTask(config);
        task->start();
        QCOMPARE(task->read(480, 2000)[1].size(), size_t(480));
        task.reset();
        QCOMPARE(errorOf([&] { e.device->aiReadStatic(AiChannel::AudioInLeft, 2.0); }), DeviceError::NotSupported);
        config.channels = {{AiChannel::AudioInLeft, 10.0}};
        QCOMPARE(errorOf([&] { e.device->createAiTask(config); }), DeviceError::InvalidConfig);
        config.channels = {{AiChannel::AudioInLeft, 2.0}, {AiChannel::AI0, 2.0}};
        QCOMPARE(errorOf([&] { e.device->createAiTask(config); }), DeviceError::InvalidConfig);
    }

    void unsupportedAi()
    {
        Emulated e;
        AiConfig config;
        config.channels = {{AiChannel::AI0, 10.0}, {AiChannel::AI0, 10.0}};
        QCOMPARE(errorOf([&] { e.device->createAiTask(config); }), DeviceError::InvalidConfig);
        config.channels = {{AiChannel::AI0, 10.0}};
        config.sampleRate = 500e3;
        QCOMPARE(errorOf([&] { e.device->createAiTask(config); }), DeviceError::InvalidConfig);
    }

    void staticAo()
    {
        Emulated e;
        e.device->aoWriteStatic(AoChannel::AO1, 0.5);
        QCOMPARE(mydaq_mock_ao_code(e.mock, 1), int16_t(1253));
        e.device->aoWriteStatic(AoChannel::AO0, 0.0);
        QCOMPARE(mydaq_mock_ao_code(e.mock, 0), int16_t(-167));
        QCOMPARE(errorOf([&] { e.device->aoWriteStatic(AoChannel::AO0, 12.0); }), DeviceError::InvalidConfig);
        QCOMPARE(errorOf([&] { e.device->aoWriteStatic(AoChannel::AudioOutLeft, 0.1); }), DeviceError::NotSupported);
        AoConfig config;
        config.channels = {{AoChannel::AO0, 2.0}};   // ±2 V AO range (timed tasks)
        config.mode = SampleMode::Finite;
        config.sampleRate = 1000;
        auto task = e.device->createAoTask(config);
        task->write({{0.5, 0.5}});
        task->start();
        QVERIFY(task->waitUntilDone(2000));
        QCOMPARE(mydaq_mock_ao_last(e.mock, 0), int16_t(7226));   // the ±2 V record: 0.5 V -> 7226
    }

    void timedAo()
    {
        Emulated e;
        mydaq_mock_set_realtime(e.mock, 1);
        AoConfig config;
        config.channels = {{AoChannel::AO0, 10.0}, {AoChannel::AO1, 10.0}};
        config.sampleRate = 20000.0;
        config.mode = SampleMode::Finite;
        std::vector<std::vector<double>> wave(2, std::vector<double>(2000, 0.25));
        wave[1].assign(2000, -0.5);
        {
            auto task = e.device->createAoTask(config);
            QCOMPARE(errorOf([&] { e.device->aoWriteStatic(AoChannel::AO0, 0.0); }), DeviceError::ResourceBusy);
            QCOMPARE(errorOf([&] { e.device->createAoTask(config); }), DeviceError::ResourceBusy);
            task->write(wave);
            task->start();
            QVERIFY(task->waitUntilDone(3000));
            QVERIFY(!task->isRunning());
            QCOMPARE(mydaq_mock_ao_output_scans(e.mock), uint64_t(2000));
            int16_t code = 0;
            mydaq_ao_task_volts_to_code(mydaq_get_calibration(e.device->handle()), MYDAQ_AO1, -0.5, &code);
            QCOMPARE(mydaq_mock_ao_last(e.mock, 1), code);
        }
        e.device->aoWriteStatic(AoChannel::AO0, 0.0);   // released

        // continuous, regenerated: no rewrite while running
        config.mode = SampleMode::Continuous;
        config.regenerate = true;
        {
            auto task = e.device->createAoTask(config);
            task->write(wave);
            task->start();
            QTest::qWait(150);
            QVERIFY(task->isRunning());
            QCOMPARE(errorOf([&] { task->write(wave); }), DeviceError::NotSupported);
            task->stop();
        }

        // streaming: the application stops feeding -> Underflow
        config.regenerate = false;
        {
            auto task = e.device->createAoTask(config);
            task->write(wave);
            task->start();
            QVERIFY(task->spaceAvailable() > 0);
            QTest::qWait(600);   // 2000 samples at 20 kS/s = 0.1 s
            QCOMPARE(errorOf([&] { task->write(wave, 100); }), DeviceError::Underflow);
            task->stop();
        }

        // audio out: ±2 V, timed only
        AoConfig audio;
        audio.channels = {{AoChannel::AudioOutLeft, 2.0}};
        audio.sampleRate = 48000.0;
        audio.mode = SampleMode::Finite;
        auto task = e.device->createAoTask(audio);
        task->write({std::vector<double>(480, 0.1)});
        task->start();
        QVERIFY(task->waitUntilDone(3000));
    }

    void dmm()
    {
        Emulated e;
        const auto dcv = e.device->dmmRanges(DmmFunction::DcVoltage);
        QCOMPARE(dcv.size(), size_t(4));
        QCOMPARE(dcv.front().label, QStringLiteral("60 V"));
        QCOMPARE(dcv.back().label, QStringLiteral("200 mV"));
        const auto ohm = e.device->dmmRanges(DmmFunction::Resistance);
        QCOMPARE(ohm.size(), size_t(6));
        QCOMPARE(ohm.front().label, QStringLiteral("20 MΩ"));
        QCOMPARE(ohm.back().label, QStringLiteral("200 Ω"));
        QCOMPARE(e.device->dmmRanges(DmmFunction::DcCurrent).front().label, QStringLiteral("1 A"));

        mydaq_mock_set_dmm_packet(e.mock, "40162;<08\r\n");
        const DmmReading r = e.device->dmmRead(DmmFunction::DcVoltage, 0.2);
        QCOMPARE(r.value, -0.018572956927664929);   // DAQmx, captures/dmm-dcv-r0.2-x2
        QVERIFY(!r.overload);
        mydaq_mock_set_dmm_packet(e.mock, "10000;=08\r\n");
        const DmmReading over = e.device->dmmRead(DmmFunction::DcVoltage, 20.0);
        QVERIFY(over.overload);
        QVERIFY(over.value < 0);
        mydaq_mock_set_dmm_packet(e.mock, "10003;804\r\n");   // captures/dmm-acv-r20-x2
        QCOMPARE(e.device->dmmRead(DmmFunction::AcVoltage, 20.0).value, 0.051250171922147272);
        QCOMPARE(errorOf([&] { e.device->dmmRead(DmmFunction::DcVoltage, 3.0); }), DeviceError::InvalidConfig);
    }

    void dio()
    {
        Emulated e;
        mydaq_mock_set_dio_inputs(e.mock, 0x3c);
        QCOMPARE(e.device->dioRead(), uint8_t(0x3c));
        e.device->dioWrite(0x01, 0x0f);
        e.device->dioSetDirection(0x0f, true);
        QCOMPARE(e.device->dioDirection(), uint8_t(0x0f));
        QCOMPARE(e.device->dioRead(), uint8_t(0x31));
        e.device->reset();
        QCOMPARE(e.device->dioDirection(), uint8_t(0));
        QCOMPARE(mydaq_mock_dio_direction(e.mock), uint8_t(0));
    }

    void unsupportedFeatures()
    {
        // Every listed feature is refused with NotSupported, and the supported basics are not listed.
        Emulated e;
        const QStringList list = e.device->unsupportedFeatures();
        auto listed = [&](const char* f) { return list.contains(QString::fromLatin1(f)); };
        auto refusedIn = [&](auto change) {
            CounterInputConfig c;
            change(c);
            return errorOf([&] { e.device->createCounterInputTask(c); }) == DeviceError::NotSupported;
        };
        auto refusedOut = [&](auto change) {
            PulseOutputConfig c;
            change(c);
            return errorOf([&] { e.device->createCounterOutputTask(c); }) == DeviceError::NotSupported;
        };
        using M = CounterMeasurement;
        QVERIFY(listed(feature::CounterBuffered) && refusedIn([](auto& c) { c.buffered = true; }));
        // The myDAQ cannot (DAQmx refuses it; the X1 decoder counts nothing with a static B level).
        QVERIFY(listed(feature::CounterExternalDirection)
                && refusedIn([](auto& c) { c.direction = CounterInputConfig::Direction::External; }));
        // Supported since the stage-3 captures: not listed, and accepted.
        auto accepted = [&](auto change) {
            CounterInputConfig c;
            change(c);
            try {
                e.device->createCounterInputTask(c);
                return true;
            } catch (const DeviceError&) {
                return false;
            }
        };
        for (const char* f : {feature::CounterPulseWidth, feature::CounterSemiPeriod, feature::CounterLinearEncoder,
                              feature::CounterAngularEncoder, feature::CounterFallingEdge, feature::CounterCountDown,
                              feature::CounterInitialCount, feature::CounterResetOnDio, feature::PulseIdleHigh,
                              feature::PulseInitialDelay, feature::CounterTwoEdgeSeparation})
            QVERIFY2(!listed(f), f);
        QVERIFY(accepted([](auto& c) { c.measurement = M::PulseWidth; c.edge = Edge::Falling; }));
        QVERIFY(accepted([](auto& c) { c.measurement = M::SemiPeriod; }));
        QVERIFY(accepted([](auto& c) { c.measurement = M::TwoEdgeSeparation; }));
        QVERIFY(accepted([](auto& c) { c.measurement = M::LinearEncoder; c.zIndexEnabled = true; }));
        QVERIFY(accepted([](auto& c) { c.measurement = M::AngularEncoder; c.decoding = CounterInputConfig::Decoding::TwoPulse; }));
        QVERIFY(accepted([](auto& c) { c.direction = CounterInputConfig::Direction::Down; c.initialCount = 100; }));
        QVERIFY(accepted([](auto& c) { c.resetOnDio1 = true; c.edge = Edge::Falling; }));
        using D = CounterInputConfig::Direction;
        // A combination the down-count emulation cannot follow is refused (not silently wrong).
        QVERIFY(refusedIn([](auto& c) { c.direction = D::Down; c.resetOnDio1 = true; c.initialCount = 10; }));
        QVERIFY(accepted([](auto& c) { c.direction = D::Down; c.resetOnDio1 = true; }));
        QVERIFY(!refusedOut([](auto& c) { c.idleHigh = true; c.initialDelay = 0.01; }));
        AoConfig ao;
        ao.channels = {{AoChannel::AO0, 2.0}, {AoChannel::AO1, 10.0}};   // ±2 V next to ±10 V
        QVERIFY(!listed(feature::AoRange2V));
        {
            auto task = e.device->createAoTask(ao);
            QCOMPARE(errorOf([&] { task->write({{2.5}, {0.0}}); }), DeviceError::InvalidConfig);   // outside ±2 V
            task->write({{1.5}, {5.0}});
        }
        ao.channels = {{AoChannel::AO0, 5.0}};
        QCOMPARE(errorOf([&] { e.device->createAoTask(ao); }), DeviceError::InvalidConfig);
        QVERIFY(!listed(feature::FrequencyOutput));
        QVERIFY(e.device->supports(QString::fromLatin1(feature::FrequencyOutput)));
        QVERIFY(!listed(feature::FrequencyOutputFastTimebases));
        QCOMPARE(e.device->coerceFrequencyOutput(1e6), 1e6);   // 10 MHz / 10
        QCOMPARE(e.device->coerceFrequencyOutput(20e6), 20e6);
        QCOMPARE(list.size(), 2);
    }

    void counters()
    {
        Emulated e;
        CounterInputConfig in;   // CountEdges on DIO0
        {
            auto task = e.device->createCounterInputTask(in);
            QCOMPARE(task->dioLinesUsed(), uint8_t(0x01));
            QCOMPARE(errorOf([&] { e.device->dioSetDirection(0x01, true); }), DeviceError::ResourceBusy);
            QCOMPARE(errorOf([&] { e.device->createCounterInputTask(in); }), DeviceError::ResourceBusy);
            task->start();
            mydaq_mock_set_counter(e.mock, 1234);
            QCOMPARE(task->readScalar(1000), 1234.0);
            QCOMPARE(errorOf([&] { task->readBuffered(10, 100); }), DeviceError::NotSupported);
            task->stop();
        }
        in.measurement = CounterMeasurement::Frequency;
        {
            auto task = e.device->createCounterInputTask(in);
            QCOMPARE(task->dioLinesUsed(), uint8_t(0x02));
            task->start();
            mydaq_mock_set_counter(e.mock, 100000);   // ticks per period
            QCOMPARE(task->readScalar(1000), 1000.0);
            mydaq_mock_set_counter(e.mock, 0);
            QCOMPARE(errorOf([&] { task->readScalar(100); }), DeviceError::Timeout);
        }
        in.measurement = CounterMeasurement::TwoEdgeSeparation;   // DIO2 -> DIO1, 100 MHz ticks
        {
            auto task = e.device->createCounterInputTask(in);
            QCOMPARE(task->dioLinesUsed(), uint8_t(0x06));
            task->start();
            mydaq_mock_set_counter(e.mock, 5000);
            QCOMPARE(task->readScalar(1000), 50e-6);
        }
        in.measurement = CounterMeasurement::CountEdges;   // down: initial - edges on the host
        in.direction = CounterInputConfig::Direction::Down;
        in.initialCount = 100;
        {
            auto task = e.device->createCounterInputTask(in);
            task->start();
            mydaq_mock_set_counter(e.mock, 30);
            QCOMPARE(task->readScalar(1000), 70.0);
            mydaq_mock_set_counter(e.mock, 101);
            QCOMPARE(task->readScalar(1000), 4294967295.0);   // wraps like the 32-bit counter
        }
        in.direction = CounterInputConfig::Direction::Up;
        in.initialCount = 0;
        in.measurement = CounterMeasurement::SemiPeriod;   // pulse time: both halves, one per read
        {
            auto task = e.device->createCounterInputTask(in);
            task->start();
            mydaq_mock_set_counter(e.mock, 5000);
            QCOMPARE(task->readScalar(1000), 50e-6);
            QCOMPARE(task->readScalar(1000), 50e-6);
        }
        in.measurement = CounterMeasurement::AngularEncoder;   // counts -> degrees
        in.decoding = CounterInputConfig::Decoding::X4;
        in.pulsesPerRevolution = 100;
        {
            auto task = e.device->createCounterInputTask(in);
            task->start();
            mydaq_mock_set_counter(e.mock, uint32_t(-200));
            QCOMPARE(task->readScalar(1000), -180.0);
        }
        in.measurement = CounterMeasurement::CountEdges;

        PulseOutputConfig out;
        out.frequency = 10000.0;
        out.dutyCycle = 0.25;
        {
            auto task = e.device->createCounterOutputTask(out);
            QCOMPARE(task->actualFrequency(), 10000.0);
            QCOMPARE(task->actualDutyCycle(), 0.25);
            task->start();
            QVERIFY(mydaq_mock_ctr_started(e.mock));
            const unsigned before = mydaq_mock_command_count(e.mock);
            task->update(1000.0, 0.5);   // live: ctr.1e + ctr.18, no stop/re-arm
            QCOMPARE(mydaq_mock_command_count(e.mock) - before, 2u);
            QVERIFY(task->isRunning());
            QCOMPARE(task->actualFrequency(), 1000.0);
            QCOMPARE(task->actualDutyCycle(), 0.5);
            task->stop();
        }
        out.mode = SampleMode::Finite;
        out.pulseCount = 10;
        {
            auto task = e.device->createCounterOutputTask(out);
            task->start();
            QVERIFY(task->waitUntilDone(1000));
        }
        out.idleHigh = true;
        out.initialDelay = 1e-3;
        {
            auto task = e.device->createCounterOutputTask(out);
            task->start();
            QVERIFY(task->waitUntilDone(1000));
        }
        out.initialDelay = -1;
        QCOMPARE(errorOf([&] { e.device->createCounterOutputTask(out); }), DeviceError::InvalidConfig);
        QCOMPARE(e.device->coerceCounterFrequency(3000.0), 1e8 / 33333.0);

        QCOMPARE(e.device->coerceFrequencyOutput(30000.0), 100e3 / 3.0);
        QCOMPARE(e.device->frequencyOutputStart(10000.0), 10000.0);
        QVERIFY(e.device->frequencyOutputRunning());
        QCOMPARE(mydaq_mock_freqout_divider(e.mock), 10);
        QCOMPARE(errorOf([&] { e.device->dioWrite(0x10, 0x10); }), DeviceError::ResourceBusy);
        e.device->frequencyOutputStop();
        QVERIFY(!e.device->frequencyOutputRunning());
        e.device->dioWrite(0x10, 0x10);
    }

    void unplug()
    {
        Emulated e;
        AiConfig config;
        config.channels = {{AiChannel::AI0, 10.0}};
        config.sampleRate = 1000.0;
        config.samplesPerChannel = 1000;
        mydaq_mock_set_realtime(e.mock, 1);
        auto task = e.device->createAiTask(config);
        task->start();
        mydaq_mock_unplug(e.mock);
        QCOMPARE(errorOf([&] { task->read(1000, 3000); }), DeviceError::NotConnected);
        QVERIFY(!e.device->isConnected());
        QCOMPARE(errorOf([&] { e.device->dioRead(); }), DeviceError::NotConnected);
        QCOMPARE(errorOf([&] { e.device->aiReadStatic(AiChannel::AI0, 10.0); }), DeviceError::NotConnected);
        task->stop();
    }

    void providerHotPlug()
    {
        auto bus = std::make_unique<FakeBus>();
        FakeBus* b = bus.get();
        {
            usb::UsbProvider provider(std::move(bus), manualOptions());
            QVERIFY(provider.scan().isEmpty());

            b->ports.push_back({"1-2", 5, "036CD1C6"});
            provider.poll();
            const QList<DevicePtr> first = provider.scan();
            QCOMPARE(first.size(), 1);
            QCOMPARE(first.front()->info().name, QStringLiteral("myDAQ (036CD1C6)"));
            provider.poll();
            QCOMPARE(provider.scan().front(), first.front());   // reused, not reopened
            QCOMPARE(b->opens, 1);

            // re-plugged between two polls: new address -> old handle dead, new device
            b->ports.front().address = 6;
            provider.poll();
            QVERIFY(!first.front()->isConnected());
            QCOMPARE(errorOf([&] { first.front()->dioRead(); }), DeviceError::NotConnected);
            QCOMPARE(provider.scan().size(), 1);
            QVERIFY(provider.scan().front() != first.front());
            QVERIFY(provider.scan().front()->isConnected());

            // a second myDAQ, in use by another program: retried until it becomes free
            b->ports.push_back({"1-3", 7, "01234567", true});
            provider.poll();
            QCOMPARE(provider.scan().size(), 1);
            b->ports.back().busy = false;
            provider.poll();
            QCOMPARE(provider.scan().size(), 2);

            // unplug everything
            const QList<DevicePtr> before = provider.scan();
            b->ports.clear();
            provider.poll();
            QVERIFY(provider.scan().isEmpty());
            for (const DevicePtr& d : before)
                QVERIFY(!d->isConnected());
        }
    }

    void providerSettleTime()
    {
        auto bus = std::make_unique<FakeBus>();
        FakeBus* b = bus.get();
        usb::UsbProvider::Options o = manualOptions();
        o.settleTime = std::chrono::milliseconds(150);
        usb::UsbProvider provider(std::move(bus), o);
        b->ports.push_back({"2-1", 3, "036CD1C6"});
        provider.poll();
        QVERIFY(provider.scan().isEmpty());   // just appeared: not opened yet
        QCOMPARE(b->opens, 0);
        QTest::qWait(200);
        provider.poll();
        QCOMPARE(provider.scan().size(), 1);
    }

    void providerQuietWithoutDevice()
    {
        // Nothing attached: nothing listed, nothing opened, nothing logged.
        auto bus = std::make_unique<FakeBus>();
        FakeBus* b = bus.get();
        usb::UsbProvider provider(std::move(bus), manualOptions());
        QTest::failOnWarning();
        for (int i = 0; i < 3; ++i) {
            provider.poll();
            QVERIFY(provider.scan().isEmpty());
        }
        QCOMPARE(b->opens, 0);
    }

    void providerDisabledByEnvironment()
    {
        qputenv("MYBENCH_NO_USB", "1");
        auto provider = makeUsbProvider();
        QVERIFY(provider->scan().isEmpty());
        qunsetenv("MYBENCH_NO_USB");
    }
};

QTEST_GUILESS_MAIN(TestUsbDevice)
#include "tst_usbdevice.moc"
