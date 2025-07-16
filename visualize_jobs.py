#!/usr/bin/env python3
"""
ThreadShell-HPC Job Visualization Tool
Generates Gantt charts and performance analytics from job logs
"""

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from datetime import datetime
import seaborn as sns
import argparse
import sys
import os

def load_job_data(log_file):
    """Load and parse job log data."""
    try:
        df = pd.read_csv(log_file)
        df['Timestamp'] = pd.to_datetime(df['Timestamp'])
        return df
    except FileNotFoundError:
        print(f"Error: Log file '{log_file}' not found.")
        return None
    except Exception as e:
        print(f"Error reading log file: {e}")
        return None

def create_gantt_chart(df, output_file='job_gantt_chart.png'):
    """Create a Gantt chart showing job execution timeline."""
    # Process data to get job start and end times
    job_events = df.pivot_table(
        index='JobID', 
        columns='Event', 
        values='Timestamp', 
        aggfunc='first'
    )
    
    # Filter jobs that have both start and completion events
    completed_jobs = job_events.dropna(subset=['STARTED'])
    
    # Calculate durations
    for event in ['COMPLETED', 'FAILED', 'KILLED']:
        if event in completed_jobs.columns:
            mask = completed_jobs[event].notna()
            completed_jobs.loc[mask, 'END_TIME'] = completed_jobs.loc[mask, event]
    
    # Use current time for still running jobs
    if 'END_TIME' not in completed_jobs.columns or completed_jobs['END_TIME'].isna().any():
        current_time = datetime.now()
        completed_jobs['END_TIME'].fillna(current_time, inplace=True)
    
    # Create the plot
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # Color map for different job types/priorities
    colors = plt.cm.Set3(range(len(completed_jobs)))
    
    for i, (job_id, row) in enumerate(completed_jobs.iterrows()):
        start_time = row['STARTED']
        end_time = row.get('END_TIME', datetime.now())
        duration = (end_time - start_time).total_seconds() / 60  # Duration in minutes
        
        # Get job details
        job_info = df[df['JobID'] == job_id].iloc[0]
        job_name = job_info.get('JobName', f'Job {job_id}')
        core_id = job_info.get('CoreID', 0)
        
        # Plot job as horizontal bar
        ax.barh(y=core_id, width=duration, left=start_time, 
                height=0.6, color=colors[i % len(colors)], 
                alpha=0.8, label=f'{job_name} (ID: {job_id})')
        
        # Add job ID text on the bar
        ax.text(start_time + pd.Timedelta(minutes=duration/2), core_id, 
                f'J{job_id}', ha='center', va='center', fontsize=8, fontweight='bold')
    
    # Customize the plot
    ax.set_xlabel('Time', fontsize=12)
    ax.set_ylabel('CPU Core ID', fontsize=12)
    ax.set_title('ThreadShell-HPC Job Execution Gantt Chart', fontsize=14, fontweight='bold')
    
    # Format x-axis
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M:%S'))
    ax.xaxis.set_major_locator(mdates.SecondLocator(interval=30))
    plt.xticks(rotation=45)
    
    # Set y-axis to show core IDs
    max_cores = df['CoreID'].max() if 'CoreID' in df.columns else 4
    ax.set_ylim(-0.5, max_cores + 0.5)
    ax.set_yticks(range(max_cores + 1))
    
    # Add grid
    ax.grid(True, alpha=0.3)
    
    # Adjust layout and save
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Gantt chart saved to: {output_file}")
    return fig

def create_performance_dashboard(df, output_file='performance_dashboard.png'):
    """Create a comprehensive performance dashboard."""
    fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
    
    # 1. Job Status Distribution
    status_counts = df['Event'].value_counts()
    ax1.pie(status_counts.values, labels=status_counts.index, autopct='%1.1f%%', startangle=90)
    ax1.set_title('Job Status Distribution', fontsize=14, fontweight='bold')
    
    # 2. Jobs Timeline
    job_timeline = df[df['Event'] == 'SUBMITTED'].copy()
    job_timeline['Hour'] = job_timeline['Timestamp'].dt.hour
    jobs_per_hour = job_timeline.groupby('Hour').size()
    
    ax2.bar(jobs_per_hour.index, jobs_per_hour.values, color='skyblue', alpha=0.7)
    ax2.set_xlabel('Hour of Day')
    ax2.set_ylabel('Number of Jobs Submitted')
    ax2.set_title('Job Submission Timeline', fontsize=14, fontweight='bold')
    ax2.grid(True, alpha=0.3)
    
    # 3. Job Duration Distribution
    if 'Duration(ms)' in df.columns:
        durations = pd.to_numeric(df['Duration(ms)'], errors='coerce').dropna()
        durations_seconds = durations / 1000  # Convert to seconds
        
        ax3.hist(durations_seconds, bins=20, color='lightgreen', alpha=0.7, edgecolor='black')
        ax3.set_xlabel('Duration (seconds)')
        ax3.set_ylabel('Number of Jobs')
        ax3.set_title('Job Duration Distribution', fontsize=14, fontweight='bold')
        ax3.grid(True, alpha=0.3)
    
    # 4. Core Utilization
    if 'CoreID' in df.columns:
        core_usage = df[df['Event'] == 'STARTED']['CoreID'].value_counts().sort_index()
        ax4.bar(core_usage.index, core_usage.values, color='orange', alpha=0.7)
        ax4.set_xlabel('CPU Core ID')
        ax4.set_ylabel('Jobs Executed')
        ax4.set_title('CPU Core Utilization', fontsize=14, fontweight='bold')
        ax4.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Performance dashboard saved to: {output_file}")
    return fig

def generate_summary_report(df, output_file='job_summary.txt'):
    """Generate a text summary report of job statistics."""
    with open(output_file, 'w') as f:
        f.write("ThreadShell-HPC Job Execution Summary Report\n")
        f.write("=" * 50 + "\n\n")
        
        # Basic statistics
        total_jobs = df['JobID'].nunique()
        f.write(f"Total Jobs Processed: {total_jobs}\n")
        
        # Job status breakdown
        f.write("\nJob Status Breakdown:\n")
        for event in ['SUBMITTED', 'STARTED', 'COMPLETED', 'FAILED', 'KILLED']:
            count = len(df[df['Event'] == event])
            f.write(f"  {event}: {count}\n")
        
        # Duration statistics
        if 'Duration(ms)' in df.columns:
            durations = pd.to_numeric(df['Duration(ms)'], errors='coerce').dropna()
            f.write(f"\nExecution Time Statistics:\n")
            f.write(f"  Average Duration: {durations.mean():.2f} ms\n")
            f.write(f"  Median Duration: {durations.median():.2f} ms\n")
            f.write(f"  Min Duration: {durations.min():.2f} ms\n")
            f.write(f"  Max Duration: {durations.max():.2f} ms\n")
        
        # Core utilization
        if 'CoreID' in df.columns:
            cores_used = df[df['Event'] == 'STARTED']['CoreID'].nunique()
            f.write(f"\nResource Utilization:\n")
            f.write(f"  CPU Cores Used: {cores_used}\n")
        
        # Time span
        time_span = df['Timestamp'].max() - df['Timestamp'].min()
        f.write(f"\nTime Span: {time_span}\n")
        
        f.write(f"\nReport generated on: {datetime.now()}\n")
    
    print(f"Summary report saved to: {output_file}")

def main():
    parser = argparse.ArgumentParser(description='ThreadShell-HPC Job Visualization Tool')
    parser.add_argument('--log-file', '-l', default='logs/job_log.csv', 
                       help='Path to job log CSV file')
    parser.add_argument('--output-dir', '-o', default='.', 
                       help='Output directory for generated files')
    parser.add_argument('--gantt', action='store_true', 
                       help='Generate Gantt chart')
    parser.add_argument('--dashboard', action='store_true', 
                       help='Generate performance dashboard')
    parser.add_argument('--report', action='store_true', 
                       help='Generate summary report')
    parser.add_argument('--all', action='store_true', 
                       help='Generate all visualizations and reports')
    
    args = parser.parse_args()
    
    # Load data
    df = load_job_data(args.log_file)
    if df is None:
        sys.exit(1)
    
    if df.empty:
        print("No job data found in log file.")
        sys.exit(1)
    
    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Generate requested outputs
    if args.all or args.gantt:
        gantt_file = os.path.join(args.output_dir, 'job_gantt_chart.png')
        create_gantt_chart(df, gantt_file)
    
    if args.all or args.dashboard:
        dashboard_file = os.path.join(args.output_dir, 'performance_dashboard.png')
        create_performance_dashboard(df, dashboard_file)
    
    if args.all or args.report:
        report_file = os.path.join(args.output_dir, 'job_summary.txt')
        generate_summary_report(df, report_file)
    
    if not any([args.gantt, args.dashboard, args.report, args.all]):
        print("No output specified. Use --all to generate all outputs.")
        parser.print_help()

if __name__ == '__main__':
    main() 