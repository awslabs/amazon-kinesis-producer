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

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.util.Collection;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

class FileAgeManager implements Runnable {

    private static final Logger log = LoggerFactory.getLogger(FileAgeManager.class);

    private static final FileAgeManager instance = new FileAgeManager();

    static synchronized FileAgeManager instance() {
        return instance;
    }

    private final ScheduledExecutorService executorService;

    private final Set<File> watchedFiles;

    FileAgeManager() {
        this(Executors.newScheduledThreadPool(1,
                new ThreadFactoryBuilder().setDaemon(true).setNameFormat("KP-FileMaintenance-%04d").build()));
    }

    FileAgeManager(ScheduledExecutorService executorService) {
        this.watchedFiles = new HashSet<>();
        this.executorService = executorService;
        this.executorService.scheduleAtFixedRate(this, 1, 1, TimeUnit.MINUTES);
    }

    public synchronized void registerFiles(Collection<File> toRegister) {
        for(File f : toRegister) {
            watchedFiles.add(f.getAbsoluteFile());
        }
    }

    @Override
    public synchronized void run() {
        for (File file : watchedFiles) {
            if (!file.exists()) {
                log.error("File '{}' doesn't exist or has been removed. "
                        + "This could cause problems with the native components.  "
                        + "It's recommended to restart the JVM.", file.getAbsolutePath());
            } else {
                if (!file.setLastModified(System.currentTimeMillis())) {
                    log.warn("Failed to update the last modified time of '{}'.", file.getAbsolutePath());
                }
            }
        }
    }
}
