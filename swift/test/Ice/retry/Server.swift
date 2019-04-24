//
// Copyright (c) ZeroC, Inc. All rights reserved.
//

import Ice
import TestCommon

class Server: TestHelperI {
    public override func run(args: [String]) throws {
        let writer = getWriter()

        let (properties, _) = try Ice.createProperties(args: args)
        properties.setProperty(key: "Ice.Warn.Dispatch", value: "0")

        let communicator = try self.initialize(properties)
        defer {
            communicator.destroy()
        }
        communicator.getProperties().setProperty(key: "TestAdapter.Endpoints", value: getTestEndpoint(num: 0))
        let adapter = try communicator.createObjectAdapter("TestAdapter")
        _ = try adapter.add(servant: RetryI(), id: Ice.stringToIdentity("retry"))
        try adapter.activate()
        serverReady()
        communicator.waitForShutdown()
    }
}
