package com.amazonaws.services.kinesis.producer;

import java.nio.ByteBuffer;
import java.util.List;
import java.util.concurrent.ExecutionException;

import com.google.common.util.concurrent.ListenableFuture;

/**
 * An interface to the native KPL daemon.
 */
public interface IKinesisProducer {

	/**
	 * Put a record asynchronously. A {@link ListenableFuture} is returned that
	 * can be used to retrieve the result, either by polling or by registering a
	 * callback.
	 * 
	 * <p>
	 * The return value can be disregarded if you do not wish to process the
	 * result. Under the covers, the KPL will automatically re-attempt puts in
	 * case of transient errors (including throttling). A failed result is
	 * generally returned only if an irrecoverable error is detected (e.g.
	 * trying to put to a stream that doesn't exist), or if the record expires.
	 *
	 * <p>
	 * <b>Thread safe.</b>
	 * 
	 * <p>
	 * To add a listener to the future:
	 * <p>
	 * <code>
	 * ListenableFuture&lt;PutRecordResult&gt; f = myKinesisProducer.addUserRecord(...);
	 * com.google.common.util.concurrent.Futures.addCallback(f, callback, executor);
	 * </code>
	 * <p>
	 * where <code>callback</code> is an instance of
	 * {@link com.google.common.util.concurrent.FutureCallback} and
	 * <code>executor</code> is an instance of
	 * {@link java.util.concurrent.Executor}.
	 * <p>
	 * <b>Important:</b>
	 * <p>
	 * If long-running tasks are performed in the callbacks, it is recommended
	 * that a custom executor be provided when registering callbacks to ensure
	 * that there are enough threads to achieve the desired level of
	 * parallelism. By default, the KPL will use an internal thread pool to
	 * execute callbacks, but this pool may not have a sufficient number of
	 * threads if a large number is desired.
	 * <p>
	 * Another option would be to hand the result off to a different component
	 * for processing and keep the callback routine fast.
	 * 
	 * @param stream
	 *            Stream to put to.
	 * @param partitionKey
	 *            Partition key. Length must be at least one, and at most 256
	 *            (inclusive).
	 * @param data
	 *            Binary data of the record. Maximum size 1MiB.
	 * @return A future for the result of the put.
	 * @throws IllegalArgumentException
	 *             if input does not meet stated constraints
	 * @throws DaemonException
	 *             if the child process is dead
	 * @see ListenableFuture
	 * @see UserRecordResult
	 * @see KinesisProducerConfiguration#setRecordTtl(long)
	 * @see UserRecordFailedException
	 */
	ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, ByteBuffer data);

	/**
	 * Put a record asynchronously. A {@link ListenableFuture} is returned that
	 * can be used to retrieve the result, either by polling or by registering a
	 * callback.
	 * 
	 * <p>
	 * The return value can be disregarded if you do not wish to process the
	 * result. Under the covers, the KPL will automatically reattempt puts in
	 * case of transient errors (including throttling). A failed result is
	 * generally returned only if an irrecoverable error is detected (e.g.
	 * trying to put to a stream that doesn't exist), or if the record expires.
	 *
	 * <p>
	 * <b>Thread safe.</b>
	 * 
	 * <p>
	 * To add a listener to the future:
	 * <p>
	 * <code>
	 * ListenableFuture&lt;PutRecordResult&gt; f = myKinesisProducer.addUserRecord(...);
	 * com.google.common.util.concurrent.Futures.addCallback(f, callback, executor);
	 * </code>
	 * <p>
	 * where <code>callback</code> is an instance of
	 * {@link com.google.common.util.concurrent.FutureCallback} and
	 * <code>executor</code> is an instance of
	 * {@link java.util.concurrent.Executor}.
	 * <p>
	 * <b>Important:</b>
	 * <p>
	 * If long-running tasks are performed in the callbacks, it is recommended
	 * that a custom executor be provided when registering callbacks to ensure
	 * that there are enough threads to achieve the desired level of
	 * parallelism. By default, the KPL will use an internal thread pool to
	 * execute callbacks, but this pool may not have a sufficient number of
	 * threads if a large number is desired.
	 * <p>
	 * Another option would be to hand the result off to a different component
	 * for processing and keep the callback routine fast.
	 * 
	 * @param stream
	 *            Stream to put to.
	 * @param partitionKey
	 *            Partition key. Length must be at least one, and at most 256
	 *            (inclusive).
	 * @param explicitHashKey
	 *            The hash value used to explicitly determine the shard the data
	 *            record is assigned to by overriding the partition key hash.
	 *            Must be a valid string representation of a positive integer
	 *            with value between 0 and <tt>2^128 - 1</tt> (inclusive).
	 * @param data
	 *            Binary data of the record. Maximum size 1MiB.
	 * @return A future for the result of the put.
	 * @throws IllegalArgumentException
	 *             if input does not meet stated constraints
	 * @throws DaemonException
	 *             if the child process is dead
	 * @see ListenableFuture
	 * @see UserRecordResult
	 * @see KinesisProducerConfiguration#setRecordTtl(long)
	 * @see UserRecordFailedException
	 */
	ListenableFuture<UserRecordResult> addUserRecord(String stream, String partitionKey, String explicitHashKey,
			ByteBuffer data);

	/**
	 * Get the number of unfinished records currently being processed. The
	 * records could either be waiting to be sent to the child process, or have
	 * reached the child process and are being worked on.
	 * 
	 * <p>
	 * This is equal to the number of futures returned from {@link #addUserRecord}
	 * that have not finished.
	 * 
	 * @return The number of unfinished records currently being processed.
	 */
	int getOutstandingRecordsCount();

	/**
	 * Get metrics from the KPL.
	 * 
	 * <p>
	 * The KPL computes and buffers useful metrics. Use this method to retrieve
	 * them. The same metrics are also uploaded to CloudWatch (unless disabled).
	 * 
	 * <p>
	 * Multiple metrics exist for the same name, each with a different list of
	 * dimensions (e.g. stream name). This method will fetch all metrics with
	 * the provided name.
	 * 
	 * <p>
	 * See the stand-alone metrics documentation for details about individual
	 * metrics.
	 * 
	 * <p>
	 * This method is synchronous and will block while the data is being
	 * retrieved.
	 * 
	 * @param metricName
	 *            Name of the metrics to fetch.
	 * @param windowSeconds
	 *            Fetch data from the last N seconds. The KPL maintains data at
	 *            per second granularity for the past minute. To get total
	 *            cumulative data since the start of the program, use the
	 *            overloads that do not take this argument.
	 * @return A list of metrics with the provided name.
	 * @throws ExecutionException
	 *             If an error occurred while fetching metrics from the child
	 *             process.
	 * @throws InterruptedException
	 *             If the thread is interrupted while waiting for the response
	 *             from the child process.
	 * @see Metric
	 */
	List<Metric> getMetrics(String metricName, int windowSeconds) throws InterruptedException, ExecutionException;

	/**
	 * Get metrics from the KPL.
	 * 
	 * <p>
	 * The KPL computes and buffers useful metrics. Use this method to retrieve
	 * them. The same metrics are also uploaded to CloudWatch (unless disabled).
	 * 
	 * <p>
	 * Multiple metrics exist for the same name, each with a different list of
	 * dimensions (e.g. stream name). This method will fetch all metrics with
	 * the provided name.
	 * 
	 * <p>
	 * The retrieved data represents cumulative statistics since the start of
	 * the program. To get data from a smaller window, use
	 * {@link #getMetrics(String, int)}.
	 * 
	 * <p>
	 * See the stand-alone metrics documentation for details about individual
	 * metrics.
	 * 
	 * <p>
	 * This method is synchronous and will block while the data is being
	 * retrieved.
	 * 
	 * @param metricName
	 *            Name of the metrics to fetch.
	 * @return A list of metrics with the provided name.
	  * @throws ExecutionException
	 *             If an error occurred while fetching metrics from the child
	 *             process.
	 * @throws InterruptedException
	 *             If the thread is interrupted while waiting for the response
	 *             from the child process.
	 * @see Metric
	 */
	List<Metric> getMetrics(String metricName) throws InterruptedException, ExecutionException;

	/**
	 * Get metrics from the KPL.
	 * 
	 * <p>
	 * The KPL computes and buffers useful metrics. Use this method to retrieve
	 * them. The same metrics are also uploaded to CloudWatch (unless disabled).
	 * 
	 * <p>
	 * This method fetches all metrics available. To fetch only metrics with a
	 * given name, use {@link #getMetrics(String)}.
	 * 
	 * <p>
	 * The retrieved data represents cumulative statistics since the start of
	 * the program. To get data from a smaller window, use
	 * {@link #getMetrics(int)}.
	 * 
	 * <p>
	 * See the stand-alone metrics documentation for details about individual
	 * metrics.
	 * 
	 * <p>
	 * This method is synchronous and will block while the data is being
	 * retrieved.
	 * 
	 * @return A list of all of the metrics maintained by the KPL.
	  * @throws ExecutionException
	 *             If an error occurred while fetching metrics from the child
	 *             process.
	 * @throws InterruptedException
	 *             If the thread is interrupted while waiting for the response
	 *             from the child process.
	 * @see Metric
	 */
	List<Metric> getMetrics() throws InterruptedException, ExecutionException;

	/**
	 * Get metrics from the KPL.
	 * 
	 * <p>
	 * The KPL computes and buffers useful metrics. Use this method to retrieve
	 * them. The same metrics are also uploaded to CloudWatch (unless disabled).
	 * 
	 * <p>
	 * This method fetches all metrics available. To fetch only metrics with a
	 * given name, use {@link #getMetrics(String, int)}.
	 * 
	 * <p>
	 * See the stand-alone metrics documentation for details about individual
	 * metrics.
	 * 
	 * <p>
	 * This method is synchronous and will block while the data is being
	 * retrieved.
	 * 
	 * @param windowSeconds
	 *            Fetch data from the last N seconds. The KPL maintains data at
	 *            per second granularity for the past minute. To get total
	 *            cumulative data since the start of the program, use the
	 *            overloads that do not take this argument.
	 * @return A list of metrics with the provided name.
	 * @throws ExecutionException
	 *             If an error occurred while fetching metrics from the child
	 *             process.
	 * @throws InterruptedException
	 *             If the thread is interrupted while waiting for the response
	 *             from the child process.
	 * @see Metric
	 */
	List<Metric> getMetrics(int windowSeconds) throws InterruptedException, ExecutionException;

	/**
	 * Immediately kill the child process. This will cause all outstanding
	 * futures to fail immediately.
	 * 
	 * <p>
	 * To perform a graceful shutdown instead, there are several options:
	 * 
	 * <ul>
	 * <li>Call {@link #flush()} and wait (perhaps with a time limit) for all
	 * futures. If you were sending a very high volume of data you may need to
	 * call flush multiple times to clear all buffers.</li>
	 * <li>Poll {@link #getOutstandingRecordsCount()} until it returns 0.</li>
	 * <li>Call {@link #flushSync()}, which blocks until completion.</li>
	 * </ul>
	 * 
	 * Once all records are confirmed with one of the above, call destroy to
	 * terminate the child process. If you are terminating the JVM then calling
	 * destroy is unnecessary since it will be done automatically.
	 */
	void destroy();

	/**
	 * Instruct the child process to perform a flush, sending some of the
	 * records it has buffered for the specified stream.
	 * 
	 * <p>
	 * This does not guarantee that all buffered records will be sent, only that
	 * most of them will; to flush all records and wait for completion, use
	 * {@link #flushSync}.
	 * 
	 * <p>
	 * This method returns immediately without blocking.
	 * 
	 * @param stream
	 *            Stream to flush
	 * @throws DaemonException
	 *             if the child process is dead
	 */
	void flush(String stream);

	/**
	 * Instruct the child process to perform a flush, sending some of the
	 * records it has buffered. Applies to all streams.
	 * 
	 * <p>
	 * This does not guarantee that all buffered records will be sent, only that
	 * most of them will; to flush all records and wait for completion, use
	 * {@link #flushSync}.
	 * 
	 * <p>
	 * This method returns immediately without blocking.
	 * 
	 * @throws DaemonException
	 *             if the child process is dead
	 */
	void flush();

	/**
	 * Instructs the child process to flush all records and waits until all
	 * records are complete (either succeeding or failing).
	 * 
	 * <p>
	 * The wait includes any retries that need to be performed. Depending on
	 * your configuration of record TTL and request timeout, this can
	 * potentially take a long time if the library is having trouble delivering
	 * records to the backend, for example due to network problems. 
	 * 
	 * <p>
	 * This is useful if you need to shutdown your application and want to make
	 * sure all records are delivered before doing so.
	 * 
	 * @throws DaemonException
	 *             if the child process is dead
	 * 
	 * @see KinesisProducerConfiguration#setRecordTtl(long)
	 * @see KinesisProducerConfiguration#setRequestTimeout(long)
	 */
	void flushSync();

}