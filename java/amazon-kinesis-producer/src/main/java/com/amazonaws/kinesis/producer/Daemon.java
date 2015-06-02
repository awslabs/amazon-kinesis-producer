/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 * http://aws.amazon.com/asl/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

package com.amazonaws.kinesis.producer;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.Channels;
import java.nio.channels.FileChannel;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.atomic.AtomicBoolean;

import javax.xml.bind.DatatypeConverter;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.amazonaws.kinesis.producer.protobuf.Messages.Message;
import com.google.protobuf.ByteString;

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
    
    private ExecutorService executor = Executors.newCachedThreadPool();
    
    private Process process = null;
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
    private final Configuration config;
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
            Configuration config, Map<String, String> environmentVariables) {
        this.pathToExecutable = pathToExecutable;
        this.handler = handler;
        this.workingDir = workingDir;
        this.config = config;
        this.environmentVariables = environmentVariables;
        
        lenBuf.order(ByteOrder.BIG_ENDIAN);
        rcvBuf.order(ByteOrder.BIG_ENDIAN);
        
        executor.submit(new Runnable() {
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
        executor.submit(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    sendMessage();
                }
            }
        });
        
        executor.submit(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    receiveMessage();
                }
            }
        });
        
        executor.submit(new Runnable() {
            @Override
            public void run() {
                while (!shutdown.get()) {
                    returnMessage();
                }
            }
        });
    }
    
    private void connectToChild() throws IOException {
        inChannel = FileChannel.open(Paths.get(inPipe.getAbsolutePath()), StandardOpenOption.READ);
        outChannel = FileChannel.open(Paths.get(outPipe.getAbsolutePath()), StandardOpenOption.WRITE);
        outStream = Channels.newOutputStream(outChannel);
    }
   
    private void createPipes() throws IOException {
        File dir = new File(this.workingDir);
        if (!dir.exists()) {
            dir.mkdirs();
        }
         
        do {
            inPipe = Paths.get(dir.getAbsolutePath(),
                    "amz-aws-kpl-in-pipe-" + UUID.randomUUID().toString().substring(0, 8)).toFile();
        } while (inPipe.exists());
        
        do {
            outPipe = Paths.get(dir.getAbsolutePath(),
                    "amz-aws-kpl-out-pipe-" + UUID.randomUUID().toString().substring(0, 8)).toFile();
        } while (inPipe.exists());
        
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
                Thread.sleep(1);
            } catch (InterruptedException e) { }
            if (System.nanoTime() - start > 1e9) {
                fatalError("Pipes did not show up after calling mkfifo", false);
            }
        }
        
        inPipe.deleteOnExit();
        outPipe.deleteOnExit();
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
        String serializedConfig = DatatypeConverter.printHexBinary(
                config.toProtobufMessage().toByteArray());
        
        final ProcessBuilder pb = new ProcessBuilder(
                pathToExecutable,
                // Our out is the child's in, vice versa
                outPipe.getAbsolutePath(),
                inPipe.getAbsolutePath(),
                serializedConfig);
        for (Entry<String, String> e : environmentVariables.entrySet()) {
            pb.environment().put(e.getKey(), e.getValue());
        }
        
        pb.redirectErrorStream(true);
        pb.redirectOutput();
        
        executor.submit(new Runnable() {
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
        
        // Redirect child stdout to our stderr
        final BufferedReader childStdout = new BufferedReader(new InputStreamReader(process.getInputStream()));
        final CountDownLatch outputComplete = new CountDownLatch(1);
        executor.submit(new Runnable() {
            @Override
            public void run() {
                try {
                    String s = "";
                    while ((s = childStdout.readLine()) != null) {
                        System.err.println(s);
                    }
                } catch (Exception e) {
                    log.error("Unexpected error transferring child console output", e);
                } finally {
                    outputComplete.countDown();
                }               
            }
        });
        
        int code = process.waitFor();
        outputComplete.await();
        deletePipes();
        fatalError("Child process exited with code " + code, code != 1);
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
                process.destroy();
            }
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
}
