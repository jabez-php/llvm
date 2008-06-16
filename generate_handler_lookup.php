<?php
/*
   +----------------------------------------------------------------------+
   | PHP LLVM extension                                                   |
   +----------------------------------------------------------------------+
   | Copyright (c) 2008 The PHP Group                                     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Joonas Govenius <joonas@php.net>                             |
   +----------------------------------------------------------------------+
*/

$fp = fopen('php://stdin', 'r');

while ($line = fgets($fp, 4096)) {
  if (preg_match("/\s*static\s+const\s+opcode_handler_t\s+labels/", $line)) {
    // start building the lookup table for function names
    break;
  }
}

$names = array();
$i = 0;
while ($line = fgets($fp, 4096)) {
  if (preg_match("/}/", $line)) {
    // done parsing the function names
    break;
  }

  $temp = split(",", $line);
  foreach ($temp as $name) {
    if (!preg_match("/^\s*$/", $name)) {
      $names[$i++] = trim($name);
    }
  }
}

fclose($fp);

// print a lookup table
$fp2 = fopen('phpllvm_handler_lookup.h', 'w');

fwrite($fp2, "/*
   +----------------------------------------------------------------------+
   | PHP LLVM extension                                                   |
   +----------------------------------------------------------------------+
   | Copyright (c) 2008 The PHP Group                                     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Joonas Govenius <joonas@php.net>                             |
   +----------------------------------------------------------------------+
*/\n\n");
fwrite($fp2, "#ifndef PHPLLVM_FUNCTION_LOOKUP_H\n");
fwrite($fp2, "#define PHPLLVM_FUNCTION_LOOKUP_H\n\n");

fwrite($fp2, "char* phpllvm_get_function_name(int i) {\n");

foreach ($names as $j => $name)
  fwrite($fp2, "  if (i == $j) return \"$name\";\n");

fwrite($fp2, "  return \"\";\n");
fwrite($fp2, "}\n\n");
fwrite($fp2, "#endif\n");

fclose($fp2);

?>