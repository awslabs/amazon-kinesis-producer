/*
 * Copyright 2019 Amazon.com, Inc. or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package software.amazon.kinesis.producer;

import software.amazon.kinesis.producer.protobuf.Messages;
import software.amazon.kinesis.producer.protobuf.Messages.Message;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.protobuf.ByteString;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.SystemUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import software.amazon.awssdk.auth.credentials.AwsCredentials;
import software.amazon.awssdk.auth.credentials.AwsCredentialsProvider;
import software.amazon.awssdk.auth.credentials.AwsSessionCredentials;

import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.Channels;
import java.nio.channels.FileChannel;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * This class manages the child process. It takes cares of starting the process,
 * connecting to it over TCP, and reading and writing messages from and to it
 * through the socket.
 * 
 * @author chaodeng
 *
 */
public class Daemon {
    private static final Logger log = LoggerFactory.getLogger(Daemon.class);
    /**
     * Callback interface used by clients to receive messages and errors.
     * 
     * @author chaodeng
     *
     */
    public static interface MessageHandler {
        public void onMessage(Message m);
        public void onError(Throwable t);
    }
    
    private BlockingQueue<Message> outgoingMessages = new LinkedBlockingQueue<>();
    private BlockingQueue<Message> incomingMessages = new LinkedBlockingQueue<>();

    private ExecutorService executor = Executors
            .newCachedThreadPool(new ThreadFactoryBuilder().setDaemon(true).setNameFormat("kpl-daemon-%04d").build());
    
    private Process process = null;
    private LogInputStreamReader stdOutReader;
    private LogInputStreamReader stdErrReader;
    private AtomicBoolean shutdown = new AtomicBoolean(false);
    
    private File inPipe = null;
    private File outPipe = null;
    private FileChannel inChannel = null;
    private FileChannel outChannel = null;
    private OutputStream outStream;

    private ByteBuffer lenBuf = ByteBuffer.allocate(4);
    private ByteBuffer rcvBuf = ByteBuffer.allocate(8 * 1024 * 1024);
    
    private final String pathToExecutable;
    private final MessageHandler handler;
    private final String workingDir;
    private final KinesisProducerConfiguration config;
    private final Map<String, String> environmentVariables;
    
    /**
     * Starts up the child process, connects to it, and beings sending and
     * receiving messages.
     * 
     * @param pathToExecutable
     *            Path to the binary that we will use to start up the child.
     * @param handler
     *            Message handler that handles messages received from the child.
     * @param workingDir
     *            Working directory. The KPL creates FIFO files; it will do so
     *            in this directory.
     * @param config
     *            KPL configuration.
     */
    public Daemon(String pathToExecutable, MessageHandler handler, String workingDir,
            KinesisProducerConfiguration config, Map<String, String> environmentVariables) {
        this.pathToExecutable = pathToExecutable;
        this.handler = handler;
        this.workingDir = workingDir;
        this.config = config;
        this.environmentVariables = environmentVariables;
        
        lenBuf.order(ByteOrder.BIG_ENDIAN);
        rcvBuf.order(ByteOrder.BIG_ENDIAN);
        
        executor.execute(new Runnable() {
            @Override
            public void run() {
                try{
                    createPipes();
                    startChildProcess();
                } catch (Exception e) {
                    fatalError("Error running child process", e);
                }
            }
        });
    }
    
    /**
     * Connect on existing pipes, without starting a child process. For testing
     * purposes.
     * 
     * @param inPipe
     *            Pipe from which we read messages from the child.
     * @param outPipe
     *            Pipe into which we write messages to the child.
     * @param handler
     *            Message handler.
     */
    protected Daemon(File inPipe, File outPipe, MessageHandler handler) {
        workingDir = ".";
        pathToExecutable = null;
        this.inPipe = inPipe;
        this.outPipe = outPipe;
        this.handler = handler;
        this.config = null;
        this.environmentVariables = null;
        
        try {
            connectToChild();
            startLoops();
        } catch (IOException e) {
            fatalError("Could not connect to child", e, false);
        }
    }
    
    /**
     * Enqueue a message to be sent to the child process.
     * 
     * @param m
     */
    public void add(Message m) {
        if (shutdown.get()) {
            throw new DaemonException(
                    "The child process has been shutdown and can no longer accept messages.");
        }
        
        try {
            outgoingMessages.put(m);
        } catch (InterruptedException e) {
            fatalError("Unexpected error", e);
        }
    }
    
    /**
     * Immediately kills the child process and shuts down the threads in this
     * Daemon.
     */
    public void destroy() {
        fatalError("Destroy is called", false);
    }
   
    public File getInPipe() {
        return inPipe;
    }    

    public File getOutPipe() {
        return outPipe;
    }     

    public String getPathToExecutable() {
        return pathToExecutable;
    }   

    public MessageHandler getHandler() {
        return handler;
    }    

    public String getWorkingDir() {
        return workingDir;
    }
    
    public int getQueueSize() {
        return outgoingMessages.size();
    }
    
    /**
     * Send a message to the child process. If there are no messages available
     * in the queue, this method blocks until there is one.
     */
    private void sendMessage()  {
        try {
            Message m = outgoingMessages.take();
            int size = m.getSerializedSize();
            lenBuf.rewind();
            lenBuf.putInt(size);
            lenBuf.rewind();
            outChannel.write(lenBuf);
            m.writeTo(outStream);
            outStream.flush();
        } catch (IOException | InterruptedException e) {
            fatalError("Error writing message to daemon", e);
        }
    }
    
    /**
     * Read a message from the child process off the wire. If there are no bytes
     * available on the socket, or there if there are not enough bytes to form a
     * complete message, this method blocks until there is.
     */
    private void receiveMessage() {
        try {
            // Read message length (4 bytes)
            readSome(4);
            int len = rcvBuf.getInt();
            if (len <= 0 || len > rcvBuf.capacity()) {
                throw new IllegalArgumentException("Invalid message size (" + len +
                        " bytes, at most " + rcvBuf.capacity() + " supported)");
            }
            
            // Read message
            readSome(len);
            
            // Deserialize message and add it to the queue
            Message m = Message.parseFrom(ByteString.copyFrom(rcvBuf));
            incomingMessages.put(m);
        } catch (IOException | InterruptedException e) {
            fatalError("Error reading message from daemon", e);
        }
    }
    
    /**
     * Invokes the message handler, giving it a message received from the child
     * process.
     */
    private void returnMessage() {
        try {
            Message m = incomingMessages.take();
            if (handler != null) {
                try {
                    handler.onMessage(m);
                } catch (Exception e) {
                    log.error("Error in message handler", e);
                }
            }
        } catch (InterruptedException e) {
            fatalError("Unexpected error", e);
        }
    }
    
    /**
     * Start up the loops that continuously send and receive messages to and
     * from the child process.
     */
    private void startLoops() {
        executor.execute(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    sendMessage();
                }
            }
        });
        
        executor.execute(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    receiveMessage();
                }
            }
        });
        
        executor.execute(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    returnMessage();
                }
            }
        });
        
        executor.execute(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    try {
                        updateCredentials();
                    } catch (InterruptedException e) {
                        //
                        // If interrupted it's likely the KPL is shutting down. So clear the
                        // interrupted status and allow the loop to continue.
                        //
                    } catch (RuntimeException re) {
                        log.error(
                                "Caught runtime exception while updating credentials.  Will retry after refresh delay",
                                re);
                    }
                    try {
                        Thread.sleep(config.getCredentialsRefreshDelay());
                    } catch (InterruptedException ie) {
                        //
                        // Same as above it's likely safe to ignore this as the KPL is probably shutting down.
                        //
                    }
                }
            }
        });
    }
    
    private void connectToChild() throws IOException {
        long start = System.nanoTime();
        while (true) {
            try {
                inChannel = FileChannel.open(Paths.get(inPipe.getAbsolutePath()), StandardOpenOption.READ);
                outChannel = FileChannel.open(Paths.get(outPipe.getAbsolutePath()), StandardOpenOption.WRITE);
                outStream = Channels.newOutputStream(outChannel);
                break;
            } catch (IOException e) {
                if (inChannel != null && inChannel.isOpen()) {
                    inChannel.close();
                }
                if (outChannel != null && outChannel.isOpen()) {
                    outChannel.close();
                }
                try {
                    Thread.sleep(100);
                } catch (InterruptedException e1) {}
                if (System.nanoTime() - start > 2e9) {
                    throw e;
                }
            }
        }
    }
   
    private void createPipes() throws IOException {
        if (SystemUtils.IS_OS_WINDOWS) {
            createPipesWindows();
        } else {
            createPipesUnix();
        }
        
        inPipe.deleteOnExit();
        outPipe.deleteOnExit();
    }
    
    private void createPipesWindows() {
        do {
            inPipe = Paths.get("\\\\.\\pipe\\amz-aws-kpl-in-pipe-" + uuid8Chars()).toFile();
        } while (inPipe.exists());
        
        do {
            outPipe = Paths.get("\\\\.\\pipe\\amz-aws-kpl-out-pipe-" + uuid8Chars()).toFile();
        } while (outPipe.exists());
    }
    
    private void createPipesUnix() {
        File dir = new File(this.workingDir);
        if (!dir.exists()) {
            dir.mkdirs();
        }
        
        do {
            inPipe = Paths.get(dir.getAbsolutePath(),
                    "amz-aws-kpl-in-pipe-" + uuid8Chars()).toFile();
        } while (inPipe.exists());
        
        do {
            outPipe = Paths.get(dir.getAbsolutePath(),
                    "amz-aws-kpl-out-pipe-" + uuid8Chars()).toFile();
        } while (outPipe.exists());
        
        try {
            Runtime.getRuntime().exec("mkfifo " + inPipe.getAbsolutePath() + " " + outPipe.getAbsolutePath());
        } catch (Exception e) {
            fatalError("Error creating pipes", e, false);
        }
        
        // The files apparently don't always show up immediately after the exec,
        // so we make sure they are there before proceeding
        long start = System.nanoTime();
        while (!inPipe.exists() || !outPipe.exists()) {
            try {
                Thread.sleep(10);
            } catch (InterruptedException e) { }
            if (System.nanoTime() - start > 15e9) {
                fatalError("Pipes did not show up after calling mkfifo", false);
            }
        }
    }
    
    private void deletePipes() {
        try {
            inChannel.close();
            outChannel.close();
            inPipe.delete();
            outPipe.delete();
        } catch (Exception e) { }
    }
    
    private void startChildProcess() throws IOException, InterruptedException {
        List<String> args = new ArrayList<>(Arrays.asList(pathToExecutable, "-o", outPipe.getAbsolutePath(), "-i",
                inPipe.getAbsolutePath(), "-c", protobufToHex(config.toProtobufMessage()), "-k",
                protobufToHex(makeSetCredentialsMessage(config.getCredentialsProvider(), false)), "-t"));

        AwsCredentialsProvider metricsCreds = config.getMetricsCredentialsProvider();
        if (metricsCreds == null) {
            metricsCreds = config.getCredentialsProvider();
        }
        args.add("-w");
        args.add(protobufToHex(makeSetCredentialsMessage(metricsCreds, true)));
        args.add("-l");
        args.add(config.getLogLevel());

        log.debug("Starting Native Process: {}", StringUtils.join(args, " "));

        final ProcessBuilder pb = new ProcessBuilder(args);
        for (Entry<String, String> e : environmentVariables.entrySet()) {
            pb.environment().put(e.getKey(), e.getValue());
        }


        executor.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    connectToChild();
                    startLoops();
                } catch (IOException e) {
                    fatalError("Unexpected error connecting to child process", e, false);
                }
            }
        });
        
        try {
            process = pb.start();
        } catch (Exception e) {
            fatalError("Error starting child process", e, false);
        }
        stdOutReader = new LogInputStreamReader(process.getInputStream(), "StdOut", new LogInputStreamReader.DefaultLoggingFunction() {
            @Override
            public void apply(Logger logger, String message) {
                logger.info(message);
            }
        });
        stdErrReader = new LogInputStreamReader(process.getErrorStream(), "StdErr", new LogInputStreamReader.DefaultLoggingFunction() {
            @Override
            public void apply(Logger logger, String message) {
                logger.warn(message);
            }
        });

        executor.execute(stdOutReader);
        executor.execute(stdErrReader);
        try {
            int code = process.waitFor();
            fatalError("Child process exited with code " + code, code != 1);
        } finally {
            stdOutReader.shutdown();
            stdErrReader.shutdown();
            deletePipes();
        }
    }
    
    private void updateCredentials() throws InterruptedException {
        outgoingMessages.put(makeSetCredentialsMessage(config.getCredentialsProvider(), false));
        AwsCredentialsProvider metricsCreds = config.getMetricsCredentialsProvider();
        if (metricsCreds == null) {
            metricsCreds = config.getCredentialsProvider();
        }
        outgoingMessages.put(makeSetCredentialsMessage(metricsCreds, true));
    }
    
    private void fatalError(String message) {
        fatalError(message, true);
    }
    
    private void fatalError(String message, boolean retryable) {
        fatalError(message, null, retryable);
    }
    
    private synchronized void fatalError(String message, Throwable t) {
        fatalError(message, t, true);
    }
    
    private synchronized void fatalError(String message, Throwable t, boolean retryable) {
        if (!shutdown.getAndSet(true)) {
            if (process != null) {
                if (stdErrReader != null) {
                    stdErrReader.prepareForShutdown();
                }
                if (stdOutReader != null) {
                    stdOutReader.prepareForShutdown();
                }
                process.destroy();
            }
            try {
                executor.awaitTermination(1, TimeUnit.SECONDS);
            } catch (InterruptedException e) { }
            executor.shutdownNow();
            if (handler != null) {
                if (retryable) {
                    handler.onError(t != null 
                            ? new RuntimeException(message, t) 
                            : new RuntimeException(message));
                } else {
                    handler.onError(t != null 
                            ? new IrrecoverableError(message, t) 
                            : new IrrecoverableError(message));
                }
            }
        }
    }
    
    private void readSome(int n) throws IOException {
        rcvBuf.rewind();
        rcvBuf.limit(n);
        int total = 0;
        while (total < n) {
            int r = inChannel.read(rcvBuf);
            if (r >= 0) {
                total += r;
            } else {
                fatalError("EOF reached during read");
            }
        }
        rcvBuf.rewind();
    }
    
    private static String uuid8Chars() {
        return UUID.randomUUID().toString().substring(0, 8);
    }

    private static Messages.Message makeSetCredentialsMessage(AwsCredentialsProvider provider, boolean forMetrics) {
        AwsCredentials creds = provider.resolveCredentials();
        
        Messages.Credentials.Builder cb = Messages.Credentials.newBuilder()
                .setAkid(creds.accessKeyId())
                .setSecretKey(creds.secretAccessKey());
        if (creds instanceof AwsSessionCredentials) {
            cb.setToken(((AwsSessionCredentials) creds).sessionToken());
        }
   
        Messages.SetCredentials setCreds = Messages.SetCredentials.newBuilder()
                .setCredentials(cb.build())
                .setForMetrics(forMetrics)
                .build();
        
        return Messages.Message.newBuilder()
            .setSetCredentials(setCreds)
            .setId(Long.MAX_VALUE)
            .build();
    }
    
    private static String protobufToHex(com.google.protobuf.Message msg) {
        return BinaryToHexConverter.convert(msg.toByteArray());
    }
}