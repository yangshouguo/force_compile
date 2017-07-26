#!/usr/bin/env python
# coding=utf-8

'''
author ： Yangshouguo
time：  2017年7月24日
mail：891584158@qq.com 
主要是通过编译某一个 .c 文件，通过编译器GCC返回的错误说明
在另一个头文件中对缺少的变量声明进行补充，然后手工在 c文件中将头文件包含进去即可。

例如 main.c 文件运行脚本会生成 main.h 文件
然后在main.c 中包含 #include<main.h>


可以解决的error类型为:
    type1:         error: unknown type name ‘ip_address’
        solution:   typedef char ip_address
    type2:          error: ‘DO_RETR’ undeclared (first use in this function)
        solution:   #define DO_RETR 1
    type3:          error: ‘opt’ undeclared (first use in this function), but  exits  opt.warc_ ;
        solution:   typedef struct { char war; ... }opt_struct;
    type4:          error: request for member ‘st’ in something not a structure or union
        solution:   see type3
    


-----------------------------------------------------------------------------------------------

    代码架构：
        1.生成源文件对应的头文件，将头文件include到源文件中，到2
        2.编译源文件，获得编译的错误信息，跳转3
        3.针对错误信息，生成对应内容，添加到头文件，到2

    用法:
    python force_compile_byerrors.py main_wget.c
'''

import sys
import os
import subprocess
# global variables
TYPE1 = 'unknown type name'
TYPE3 = 'undeclared (first use in this function), but  exits'
TYPE2 = 'undeclared (first use in this function)'
TYPE4 = 'request for member'
filename_pre = ''
define_num = 1
struct_mem = {}
#
def normalize_variable_name(name):
    
    newname =''
    for i in range(len(name)):
        t = name[i]
        if (t>='0' and t<='9') or (t>='a' and t<='z') or (t>='A' and t<='Z') or t=='_':
            newname+=t

    return newname


def handle_type_error(type1,var_name,args = []):
    global filename_pre,define_num,struct_mem
    fp = open(filename_pre,'a+')
    
    #print "var_name:"+var_name
    if(type1 == 1):
        content = 'typedef char %s; \n' % normalize_variable_name(var_name)
        fp.write(content)
        fp.close()
    elif(type1 == 2):
        content = '#define %s %d \n' % (normalize_variable_name(var_name),define_num)
        define_num+=1
        fp.write(content)
        fp.close()
    elif(type1 ==4):
        content = 'typedef struct {\n '
        if(not struct_mem.has_key(var_name)):
            print 'error! no such key in dic'
            return
        memset = set(struct_mem[var_name])
        for x in memset:
            content+= 'char %s ;\n'%x

        content += ' } %s_struct;\n'%var_name
        content+= '%s_struct %s ; \n' %(var_name,var_name)
        print content

        fp.write(content)
        fp.close()

 

def getstart(str1,b):
    i = b-2
    while i>=0:
        if(str1[i]>='a' and str1[i]<='z') or (str1[i]<='Z' and str1[i]>='A') or (str1[i]<='0' and str1[i]>='9') or str1[i] =='_':
            i-=1
        else:
            return i+1

def analyse(str1):
    global TYPE1,TYPE2,TYPE3,TYPE4,struct_mem 

    if str1.find(TYPE1)!=-1:
        #print str1
        a1 = str1.find('‘')
        a2 = str1.find('’',a1+1)
        var_name = str1[a1+1:a2]
        handle_type_error(1,var_name)
        print 'TYPE1'
    elif str1.find(TYPE3)!=-1:
        print 'TYPE3'
    elif str1.find(TYPE2)!=-1:
        print 'TYPE2'
        s = str1.find('error')
        if(s!=-1):
            a1 = str1.find('‘',s)
            a2 = str1.find('’',a1+1)
            var_name = str1[a1+1:a2]
            handle_type_error(2,var_name)
    elif str1.find(TYPE4)!=-1:
        print 'TYPE4'
        s = str1.find('error')
        if(s!=-1):
            a1 = str1.find('‘',s)
            a2 = str1.find('’',a1+1)
            var_name=''
            var_name = str1[a1+1:a2]
            var_name = var_name[2:]
            b2 = str1.rfind('.'+var_name)
            if(b2==-1):
                b2 = str1.rfind('->'+var_name)
            print str1
            if (b2!=-1):
                b1 = getstart(str1,b2)
                key = str1[b1:b2]
                print 'key is ' ,key
                if (struct_mem.has_key(key)):
                    struct_mem[key].append(var_name)
                else:
                    struct_mem.update({key:[var_name]})
            #handle_type_error(2,var_name)
    else:
        #print str1
        a= 1

def create_strcut():
    global struct_mem
    for keyx in struct_mem.keys():
        handle_type_error(4,keyx)

def handle_error(errorinfo):
    print 'start -------------'
    #print errorinfo

    errors = errorinfo.split('^')
    for item in errors:
        analyse(item)
    global struct_mem
    create_strcut()
    print 'end- --------------'
    return 0
#编译c文件，如果编译通过返回 True ，否则返回False
def compile_cfile(filename):
    cmd = 'gcc -S %s '%filename
    #compile_output = os.popen(cmd)
    compile_output = subprocess.Popen(cmd,shell=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    q = compile_output.stderr.read()
    p = q.replace('\\x..','')
    return handle_error(p)

def print_help():
    print 'please use this script as:python force_compile_byerrors.py  cfile'


#main
def main():
    
    global filename_pre 

    filename = ''#待编译的c文件名
    if len(sys.argv) != 2:
        print_help()
    else:
        filename = sys.argv[1]
    seq = filename.find('.c')

    if(seq == -1):
        print 'cfilename error'
        return 
    
    filename_pre = filename[:seq]
    filename_pre += '.h'
    
    createfile_cmd = 'touch %s'% filename_pre
    os.system(createfile_cmd)
    
    # 在c文件中添加头文件
    file_c = open(filename,'r')
    first_line = file_c.readline()
    insert_content = '#include "%s"' % filename_pre
    if(first_line not in insert_content):
        file_c_content = file_c.read() 
        file_c.close()
        a = file_c_content.split('\n')
        a.insert(0,insert_content)
        s = '\n'.join(a)
        file_c = file(filename,'w')
        file_c.write(s)
        file_c.close()

    compile_cfile(filename)


if __name__ == "__main__":

    main()



