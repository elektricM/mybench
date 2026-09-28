// Demo instrument of the controls gallery: DC voltage readings from the (simulated) DMM.
#pragma once

#include <mybench/instrumentcontroller.h>
#include <mybench/instrumentregistry.h>

#include <QtQml/qqmlregistration.h>

namespace mybench::gallery {

class DemoController : public InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")
    Q_PROPERTY(double reading READ reading NOTIFY readingChanged)
    Q_PROPERTY(bool overload READ overload NOTIFY readingChanged)
    Q_PROPERTY(double frequency READ frequency WRITE setFrequency NOTIFY frequencyChanged)
    Q_PROPERTY(int timeDivIndex READ timeDivIndex WRITE setTimeDivIndex NOTIFY timeDivIndexChanged)

public:
    DemoController();
    ~DemoController() override;

    double reading() const { return m_reading; }
    bool overload() const { return m_overload; }
    double frequency() const { return m_frequency; }
    void setFrequency(double frequency);
    int timeDivIndex() const { return m_timeDivIndex; }
    void setTimeDivIndex(int index);

signals:
    void readingChanged();
    void frequencyChanged();
    void timeDivIndexChanged();

protected:
    WorkerFunction createWorker() override;

private:
    double m_reading = 0;
    bool m_overload = false;
    double m_frequency = 1000;
    int m_timeDivIndex = 9;
};

void registerDemo(InstrumentRegistry& registry);

}  // namespace mybench::gallery
