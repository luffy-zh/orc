/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
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

package org.apache.orc.tools;

import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hive.ql.exec.vector.LongColumnVector;
import org.apache.hadoop.hive.ql.exec.vector.VectorizedRowBatch;
import org.apache.orc.OrcFile;
import org.apache.orc.TestConf;
import org.apache.orc.TypeDescription;
import org.apache.orc.Writer;
import org.apache.orc.tools.RowCount;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.util.LinkedHashMap;
import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertTrue;

public class TestRowCount implements TestConf {
  private Path workDir = new Path(
      Paths.get(System.getProperty("test.tmp.dir"), "orc-test-count").toString());
  private FileSystem fs;

  @BeforeEach
  public void openFileSystem() throws Exception {
    fs = FileSystem.getLocal(conf);
    fs.mkdirs(workDir);
    fs.deleteOnExit(workDir);
  }

  @Test
  public void testCount() throws Exception {
    TypeDescription schema = TypeDescription.fromString("struct<x:int>");
    Map<String, Integer> fileToRowCountMap = new LinkedHashMap<>();
    fileToRowCountMap.put(workDir + File.separator + "test-count-1.orc", 10000);
    fileToRowCountMap.put(workDir + File.separator + "test-count-2.orc", 20000);
    for (Map.Entry<String, Integer> fileToRowCount : fileToRowCountMap.entrySet()) {
      Writer writer = OrcFile.createWriter(new Path(fileToRowCount.getKey()),
          OrcFile.writerOptions(conf)
              .setSchema(schema));
      VectorizedRowBatch batch = schema.createRowBatch();
      LongColumnVector x = (LongColumnVector) batch.cols[0];
      for (int r = 0; r < fileToRowCount.getValue(); ++r) {
        int row = batch.size++;
        x.vector[row] = r;
        if (batch.size == batch.getMaxSize()) {
          writer.addRowBatch(batch);
          batch.reset();
        }
      }
      if (batch.size != 0) {
        writer.addRowBatch(batch);
      }
      writer.close();
    }

    PrintStream origOut = System.out;
    ByteArrayOutputStream myOut = new ByteArrayOutputStream();
    // replace stdout and run command
    System.setOut(new PrintStream(myOut, false, StandardCharsets.UTF_8));
    RowCount.main(conf, new String[]{workDir.toString()});
    System.out.flush();
    System.setOut(origOut);
    String output = myOut.toString(StandardCharsets.UTF_8);
    assertTrue(output.contains(" 10000"));
    assertTrue(output.contains(" 20000"));
  }
}
