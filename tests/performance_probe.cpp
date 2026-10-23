#include "core/core.h"
#include "core/store.h"
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <ctime>
#include <iostream>
static int passageStartupProfile() {
    constexpr int samples = 10;
    reword::Options options;
    const QString input = "im here with teh book";
    const QString expected = "I am here with the book";
    bool correct = true;
    QElapsedTimer wall;
    wall.start();
    auto cpu = std::clock();
    for (int i = 0; i < samples; ++i) {
        reword::Engine engine;
        auto result = engine.text(input, options);
        correct = correct && result.error.isEmpty() && result.corrected == expected;
    }
    const double freshCpu = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
    const double freshWall = wall.nsecsElapsed() / 1000000.0;
    reword::Engine reused;
    wall.restart();
    cpu = std::clock();
    for (int i = 0; i < samples; ++i) {
        auto result = reused.text(input, options);
        correct = correct && result.error.isEmpty() && result.corrected == expected;
    }
    const double reusedCpu = 1000.0 * (std::clock() - cpu) / CLOCKS_PER_SEC;
    QJsonObject metrics{{"scope", "synthetic-passage-startup-diagnostic"},
                        {"samples", samples}, {"correct", correct},
                        {"freshCpuMsTotal", freshCpu}, {"freshWallMsTotal", freshWall},
                        {"reusedCpuMsTotal", reusedCpu},
                        {"reusedWallMsTotal", wall.nsecsElapsed() / 1000000.0}};
    std::cout << QJsonDocument(metrics).toJson(QJsonDocument::Indented).constData();
    return correct ? 0 : 1;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--settings-read-profile")) {
        QTemporaryDir data;
        if (!data.isValid())
            return 2;
        reword::Store store(data.path());
        store.setValue("english", false);
        store.setValue("maxCharacters", 999999);
        constexpr int samples = 1000;
        const auto started = std::clock();
        bool correct = true;
        for (int i = 0; i < samples; ++i) {
            const auto options = store.options();
            correct = correct && !options.english && options.persian &&
                      options.maxCharacters == reword::MaxPassageCharacters;
        }
        const double cpu = 1000.0 * (std::clock() - started) / CLOCKS_PER_SEC;
        std::cout << QJsonDocument(QJsonObject{{"samples", samples}, {"correct", correct},
                                             {"optionsReadCpuMs", cpu}})
                         .toJson(QJsonDocument::Indented).constData();
        return correct ? 0 : 1;
    }
    if (app.arguments().contains("--passage-startup-profile"))
        return passageStartupProfile();
    reword::Engine engine;
    reword::Options options;
    QStringList samples{"teh",
                        "recieve",
                        "hello",
                        "helllo",
                        "wrld",
                        "because",
                        "seperate",
                        "adress",
                        "langauge",
                        "qzxv",
                        QString::fromUtf8("سلام"),
                        QString::fromUtf8("سلاام"),
                        QString::fromUtf8("برناهم"),
                        QString::fromUtf8("کتاب")};
    QList<qint64> timings;
    QElapsedTimer timer;
    for (int round = 0; round < 100; ++round)
        for (auto word : samples) {
            timer.start();
            engine.word(word, options);
            timings.append(timer.nsecsElapsed());
        }
    std::sort(timings.begin(), timings.end());
    double p95 = timings[(timings.size() - 1) * 95 / 100] / 1000000.0;
    auto text = QString("teh ").repeated(12500);
    timer.start();
    auto result = engine.text(text, options);
    double passage = timer.elapsed();
    QJsonObject metrics{{"samples", timings.size()},
                        {"wordP95Ms", p95},
                        {"wordMaxMs", timings.last() / 1000000.0},
                        {"passageCharacters", text.size()},
                        {"passageMs", passage},
                        {"passageCompleted", result.error.isEmpty()}};
    std::cout << QJsonDocument(metrics).toJson(QJsonDocument::Indented).constData();
    return p95 < 50 && result.error.isEmpty() ? 0 : 1;
}
