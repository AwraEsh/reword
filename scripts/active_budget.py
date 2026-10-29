"""Keep acceptance budgets strict; bounded diagnostics may continue CPU failures."""
from stability_evidence import WorkloadFailure


def enforce_budget(sample, role, baseline, diagnostic_cpu, evidence, metadata):
    rss_limit = (512 if role == 'app' else 256) * 1024
    if (sample['rssKiB'] > rss_limit or sample['swapKiB'] > 0 or
            (baseline and sample['privateGrowthKiB'] > 64 * 1024)):
        raise WorkloadFailure('resource-budget')
    if baseline and sample['elapsedSeconds'] >= 30 and sample['cpuPercent'] > 10:
        if not diagnostic_cpu:
            raise WorkloadFailure('resource-budget')
        metadata['cpuBudgetExceeded'] = True
        evidence.fail('resource-budget')
