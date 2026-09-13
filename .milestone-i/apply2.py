from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "apps/ied_simulator/src/IedSignalModel.cpp"
text = PATH.read_text(encoding="utf-8")

if "refreshRequestCount_" not in text:
    old = '''void IedSignalModel::scheduleRefresh() {
    // Latest-state presentation accumulator: any number of valuesChanged bursts
    // inside one frame collapse into one scan of only the currently projected
    // source rows. Protocol/runtime state remains ordered and authoritative.
    if (!rebuildTimer_.isActive() && !refreshTimer_.isActive()) refreshTimer_.start();
}
'''
    new = '''void IedSignalModel::scheduleRefresh() {
    // Latest-state presentation accumulator: any number of valuesChanged bursts
    // inside one frame collapse into one scan of only the currently projected
    // source rows. Protocol/runtime state remains ordered and authoritative.
    ++refreshRequestCount_;
    if (!rebuildTimer_.isActive() && !refreshTimer_.isActive()) {
        refreshLatencyTimer_.restart();
        refreshTimer_.start();
    }
}

void IedSignalModel::resetPerformanceCounters() {
    refreshRequestCount_ = 0;
    refreshFlushCount_ = 0;
    lastRefreshLatencyMilliseconds_ = 0;
    maxRefreshLatencyMilliseconds_ = 0;
    lastRebuildMilliseconds_ = 0;
    maxRebuildMilliseconds_ = 0;
    refreshLatencyTimer_.invalidate();
    emit performanceCountersChanged();
}
'''
    if old not in text:
        raise RuntimeError("scheduleRefresh anchor missing")
    text = text.replace(old, new, 1)

    old = '''void IedSignalModel::rebuild() {
    const int previousCount = rowCount();
'''
    new = '''void IedSignalModel::rebuild() {
    QElapsedTimer rebuildElapsed;
    rebuildElapsed.start();
    const int previousCount = rowCount();
'''
    if old not in text:
        raise RuntimeError("rebuild start anchor missing")
    text = text.replace(old, new, 1)

    old = '''    endResetModel();
    if (previousCount != rowCount()) emit visibleRowCountChanged();
}

void IedSignalModel::refreshSnapshot() {
'''
    new = '''    endResetModel();
    if (previousCount != rowCount()) emit visibleRowCountChanged();
    lastRebuildMilliseconds_ = rebuildElapsed.elapsed();
    maxRebuildMilliseconds_ = std::max(maxRebuildMilliseconds_, lastRebuildMilliseconds_);
    emit performanceCountersChanged();
}

void IedSignalModel::refreshSnapshot() {
    ++refreshFlushCount_;
    if (refreshLatencyTimer_.isValid()) {
        lastRefreshLatencyMilliseconds_ = refreshLatencyTimer_.elapsed();
        maxRefreshLatencyMilliseconds_ =
            std::max(maxRefreshLatencyMilliseconds_, lastRefreshLatencyMilliseconds_);
        refreshLatencyTimer_.invalidate();
    }
    emit performanceCountersChanged();
'''
    if old not in text:
        raise RuntimeError("rebuild end anchor missing")
    text = text.replace(old, new, 1)

    PATH.write_text(text, encoding="utf-8")
    print("IedSignalModel.cpp: instrumented")
else:
    print("IedSignalModel.cpp: already instrumented")
