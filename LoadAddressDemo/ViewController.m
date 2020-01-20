//
//  ViewController.m
//  LoadAddressDemo
//
//  Created by BeyondChao on 2020/1/13.
//  Copyright © 2020 BeyondChao. All rights reserved.
//  

#import "ViewController.h"
#import "GIOMonitorCrashDynamicLinker.h"
#import "GIOMonitorCrash.h"
#import "MBProgressHUD.h"
#import "GIOMonitorCrashMonitor_NSException.h"
#import <objc/runtime.h>

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    [self setupCrash];
    
    SEL selector = NSSelectorFromString(@"crash");
    Method originalMethod = class_getClassMethod(MBProgressHUD.class, selector);
    NSLog(@"method imp = %p\n", method_getImplementation(originalMethod));
    
}

- (void)setupCrash {
    [[GIOMonitorCrash sharedInstance] install];
}

- (IBAction)launchBtnClick:(UIButton *)sender {
    
    const int imageCount = gioMonitorCrashDynamicLinker_imageCount();

    NSLog(@"image count = %d", imageCount);

    for (int iImg = 0; iImg < imageCount; iImg++) {

        GIOMonitorCrashBinaryImage image = {0};

        if(!gioMonitorCrashDynamicLinker_getBinaryImage(iImg, &image)) {
            return;
        }
        
        NSString *imageName = [[NSString alloc] initWithUTF8String:image.name];
        
        if (![imageName containsString:@"LoadAddressDemo.app"]) {
            continue;
        }
        NSLog(@"name = %@", imageName);
        NSLog(@"address = %@", hexAddress([NSNumber numberWithUnsignedLong:image.address]));
        NSLog(@"image size = %llu", image.size);
        NSLog(@"uuid = %s", uuidBytesToString(image.uuid));

        NSLog(@"\n------------\n");
    }
    
//    [self testMethod];
    
 }

static inline NSString *hexAddress(NSNumber *value) {
    return [NSString stringWithFormat:@"0x%016llx", [value unsignedLongLongValue]];
}

static const char* uuidBytesToString(const uint8_t* uuidBytes) {
    CFUUIDRef uuidRef = CFUUIDCreateFromUUIDBytes(NULL, *((CFUUIDBytes*)uuidBytes));
    NSString* str = (__bridge_transfer NSString*)CFUUIDCreateString(NULL, uuidRef);
    CFRelease(uuidRef);

    return [str UTF8String];
}

- (void)testMethod {
    NSLog(@"growing ------ func = %s, line = %d", __func__, __LINE__);
    
    NSException *exception = [[NSException alloc] initWithName:@"Test_Load_Address"
                                                        reason:@"No Reason"
                                                      userInfo:@{@"usage": @"test"}];
    handleException(exception, NO);
}

@end
