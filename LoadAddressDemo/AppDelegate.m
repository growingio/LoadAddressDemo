//
//  AppDelegate.m
//  LoadAddressDemo
//
//  Created by BeyondChao on 2020/1/13.
//  Copyright © 2020 BeyondChao. All rights reserved.
//

#import "AppDelegate.h"

@interface AppDelegate ()

@end

@implementation AppDelegate

//static void handleUncaughtException(NSException* exception) {
//    NSArray* addresses = [exception callStackReturnAddresses];
//    NSArray* symbols = [exception callStackSymbols];
//
//    NSLog(@"addresses = %@, symbols = %@", addresses, symbols);
//
//    NSArray *threadAddresses = [NSThread callStackReturnAddresses];
//    NSArray *threadSymbols = [NSThread callStackSymbols];
//
//    NSLog(@"threadAddresses = %@, threadSymbols = %@", threadAddresses, threadSymbols);
//}

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    
    NSLog(@"HOME DIR = %@", NSHomeDirectory());
//    NSSetUncaughtExceptionHandler(&handleUncaughtException);
    return YES;
}

@end
