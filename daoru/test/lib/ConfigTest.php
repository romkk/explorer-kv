<?php

class ConfigTest extends PHPUnit_Framework_TestCase {
    protected function setUp() {
        Config::put('unittest', null);
    }

    public function testGetPutConfig() {
        $this->assertNull(Config::get('unittest'));

        Config::put('unittest', 'test');
        $this->assertEquals('test', Config::get('unittest'));

        Config::put('unittest.name', 'test');
        $this->assertEquals('test', Config::get('unittest.name'));

        Config::put('unittest.name', [
            'k' => 'v'
        ]);
        $this->assertArrayHasKey('k', Config::get('unittest.name'));
        $this->assertEquals('v', Config::get('unittest.name.k'));
    }

    public function testLoadConfig() {
        $this->assertNotNull(Config::get('test'));
        $this->assertTrue(Config::get('test.valid'));
        $this->assertTrue(Config::get('test.dict.valid'));
    }
}